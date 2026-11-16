#include "mod_loader.h"

#include <kernel/function.h>
#include <kernel/heap.h>
#include <user/paths.h>
#include <xxHashMap.h>
#include <nlohmann/json.hpp>
#include <mod/IniFile.h>
#include <os/logger.h>
#include <user/config.h>
#include <cctype>
#include <algorithm>
#include <patches/global.h>
#include <kernel/io/file_system.h>


namespace fs = std::filesystem;

// for append tex
struct TxdTexture
{
	std::string name;
	std::vector<uint8_t> chunk; // whole chunk, header included
};

// Used to store the mod's file replacement data
struct ModArchiveWorker
{
	std::string archive; // ex: .one file
	std::string asset;  // og asset name
	std::filesystem::path file;
	bool isWildcard; // used for massive assets replacement in different archives at once, ex: "event/*"
	bool fromIncludeDir; // for codeless config folders, they get priority over the mod root data
	bool isAppend = false; // "TEXTURE.TXD+=" merge textures into the og txd instead of replacing it
	std::vector<TxdTexture> textures; // textures to merge, read once at boot
};

struct Mod
{
	std::string Name;
	std::string Author;
	std::string Description;
	std::string Version;
	std::string Folder;
	std::string ID;
	bool MainSaveRedirect;

	std::vector<std::filesystem::path> includeDirs; // for codeless config
	std::filesystem::path commonDir; // for regular game files replacement that run regardless of config option
	std::vector<ModArchiveWorker> archiveWorker; // for individual file replacement in archives
	xxHashMap<std::vector<uint32_t>> replaceRulesByAsset; // key: asset name hash, value: index in replaceRules
};

enum class ModReservedID : uint8_t
{
	Reloaded,
	Multiplayer
};

const char* reservedIDName[] =
{
	"shthr.reloaded",
	"shthr.multiplayer"
};

static std::vector<Mod> g_mods;


static std::mutex g_afsMutex; // I'm paranoid
static std::unordered_map<std::string, std::filesystem::path> g_afsReplacedFiles; // used on startup to check which files are replaced by mods
static std::unordered_map<std::string, std::vector<std::string>> g_afsEntryNames; 


std::filesystem::path ModLoader::ResolvePath(std::string_view path)
{
	std::string_view root;

	size_t sepIndex = path.find(":\\");
	if (sepIndex != std::string_view::npos)
	{
		root = path.substr(0, sepIndex);
		path.remove_prefix(sepIndex + 2);
	}


	if (g_mods.empty())
		return {};

	thread_local xxHashMap<std::filesystem::path> s_cache;

	XXH64_hash_t hash = XXH3_64bits(path.data(), path.size());
	auto findResult = s_cache.find(hash);
	if (findResult != s_cache.end())
		return findResult->second;

	std::string pathStr(path);
	std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

	// afs stuff
	{
		std::string lowerPath = pathStr;
		std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char c) { return (char)std::tolower(c); });

		std::lock_guard lock(g_afsMutex);
		auto afsIt = g_afsReplacedFiles.find(lowerPath);
		if (afsIt != g_afsReplacedFiles.end())
			return s_cache.emplace(hash, afsIt->second).first->second;
	}

	std::filesystem::path fsPath(std::move(pathStr));


	for (int i = g_mods.size() - 1; i >= 0; i--) // iterate in reverse to prioritize later mods in the list (which are higher priority in mod loader)
	{
		auto& mod = g_mods[i];

		for (auto& includeDir : mod.includeDirs)
		{
			std::filesystem::path modPath = (includeDir / fsPath).lexically_normal();
			if (std::filesystem::exists(modPath))
				return s_cache.emplace(hash, modPath).first->second;
		}

		std::filesystem::path commonDir = mod.commonDir;
		std::filesystem::path modPath = (commonDir / fsPath).lexically_normal();
		if (std::filesystem::exists(modPath))
			return s_cache.emplace(hash, modPath).first->second;
	}

	return s_cache.emplace(hash, std::filesystem::path{}).first->second;
}

std::vector<std::filesystem::path>* ModLoader::GetIncludeDirectories(size_t modIndex)
{
	return modIndex < g_mods.size() ? &g_mods[modIndex].includeDirs : nullptr;
}

static std::string ToLower(std::string str)
{
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return str;
}

static void Trim(std::string& str)
{
	size_t start = str.find_first_not_of(" \t\r\n");
	size_t end = str.find_last_not_of(" \t\r\n");
	str = start == std::string::npos ? std::string() : str.substr(start, end - start + 1);
}

// Lower asset name because the game is not consistent with that for some reason.
static XXH64_hash_t HashLowerName(std::string name)
{
	name = ToLower(std::move(name));
	return XXH3_64bits(name.c_str(), name.size());
}

// The game is inconsistent with cases and slashes in archive names (eg: "event\event2201_sceneA.one", "EVENT/...SCENEA.ONE").
// so force accurate matching.
static std::string NormalizeArchiveName(std::string name)
{
	name = ToLower(std::move(name));
	std::replace(name.begin(), name.end(), '\\', '/');

	size_t drive = name.find(":/");

	if (drive != std::string::npos)
		name.erase(0, drive + 2);

	while (name.starts_with("./"))
		name.erase(0, 2);

	while (name.starts_with('/'))
		name.erase(0, 1);

	if (name.ends_with(".one"))
		name.resize(name.size() - 4);

	return name;
}


static bool MatchWildcard(std::string_view pattern, std::string_view str)
{
	size_t p = 0;
	size_t s = 0;
	size_t starP = std::string_view::npos;
	size_t starS = 0;

	while (s < str.size())
	{
		if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == str[s]))
		{
			p++;
			s++;
		}
		else if (p < pattern.size() && pattern[p] == '*')
		{
			starP = p++;
			starS = s;
		}
		else if (starP != std::string_view::npos)
		{
			p = starP + 1;
			s = ++starS;
		}
		else
		{
			return false;
		}
	}

	while (p < pattern.size() && pattern[p] == '*')
	{
		p++;
	}

	return p == pattern.size();
}

static bool isMatchingName(const ModArchiveWorker& modWK, const std::string& archive)
{
	return modWK.isWildcard ? MatchWildcard(modWK.archive, archive) : modWK.archive == archive;
}

// Handle priority for file replacement rules
// codeless config folder get priority over mod files in root folder
// And a single archive replacement beats wildcard.
static bool handleRulesPriority(const ModArchiveWorker& a, const ModArchiveWorker& b)
{
	if (a.fromIncludeDir != b.fromIncludeDir)
		return a.fromIncludeDir;

	if (a.isWildcard != b.isWildcard)
		return !a.isWildcard;

	if (a.archive.size() != b.archive.size())
		return a.archive.size() > b.archive.size();

	return a.archive < b.archive;
}

// RenderWare chunk ids (used by texture dictionaries)
constexpr uint32_t RW_PLATFORM_XBOX = 5;

enum RwChunkType
{
	RW_STRUCT_TYPE = 0x01,
	RW_STRING_TYPE = 0x02,
	RW_EXTENSION_TYPE = 0x03,
	RW_CAMERA_TYPE = 0x04,
	RW_TEXTURE_NATIVE_TYPE = 0x15,
	RW_TEX_DICTIONARY_TYPE = 0x16,
};

struct ParsedTxdData
{
	uint32_t version;
	uint16_t deviceId;
	std::vector<TxdTexture> textures;
	std::vector<uint8_t> extension; // whole chunk
};

// Browse current TexDictionary and copy its native textures.
static bool ParseTxd(std::span<const uint8_t> data, ParsedTxdData &txd, std::string& error)
{
	auto FuncReadU32 = [&](size_t pos) { uint32_t v; memcpy(&v, data.data() + pos, 4); return v; };

	if (data.size() < 28 || FuncReadU32(0) != RW_TEX_DICTIONARY_TYPE || FuncReadU32(12) != RW_STRUCT_TYPE || FuncReadU32(16) < 4)
	{
		error = "not a texture dictionary";
		return false;
	}

	size_t end = std::min<size_t>(data.size(), 12 + size_t(FuncReadU32(4)));
	txd.version = FuncReadU32(8);
	txd.deviceId = uint16_t(FuncReadU32(24) >> 16);
	size_t pos = 24 + FuncReadU32(16);

	while (pos + 12 <= end)
	{
		uint32_t type = FuncReadU32(pos);
		size_t chunkSize = 12 + size_t(FuncReadU32(pos + 4));

		if (pos + chunkSize > end)
		{
			error = "chunk goes past the end of the file";
			return false;
		}

		if (type == RW_TEXTURE_NATIVE_TYPE)
		{
			if (chunkSize < 12 + 12 + 40 || FuncReadU32(pos + 12) != RW_STRUCT_TYPE)
			{
				error = "broken texture chunk";
				return false;
			}

			if (FuncReadU32(pos + 24) != RW_PLATFORM_XBOX)
			{
				error = fmt::format("texture is not Xbox platform (platform {})", FuncReadU32(pos + 24));
				return false;
			}

			auto name = reinterpret_cast<const char*>(data.data() + pos + 32);
			txd.textures.push_back({ ToLower(std::string(name, strnlen(name, 32))), std::vector<uint8_t>(data.begin() + pos, data.begin() + pos + chunkSize) });
		}
		else if (type == RW_EXTENSION_TYPE)
		{
			txd.extension.assign(data.begin() + pos, data.begin() + pos + chunkSize);
		}

		pos += chunkSize;
	}

	return true;
}

static void WriteChunkHeader(std::vector<uint8_t>& out, uint32_t type, uint32_t size, uint32_t version)
{
	uint32_t header[3] = { type, size, version };
	out.insert(out.end(), reinterpret_cast<uint8_t*>(header), reinterpret_cast<uint8_t*>(header) + sizeof(header));
}

// Same name replaces the og texture 
// a new name however is added at the end of the txd.
static std::vector<uint8_t> MergeTxd(ParsedTxdData &base, const std::vector<const ModArchiveWorker*>& appends, const std::string& archive)
{
	for (auto modWK : appends)
	{
		for (auto& texture : modWK->textures)
		{
			auto it = std::find_if(base.textures.begin(), base.textures.end(), [&](const TxdTexture& t) { return t.name == texture.name; });

			if (it != base.textures.end())
			{
				LOGF_IMPL(Utility, "Mod Loader", "[{}] {}: texture {} replaced", archive, modWK->asset, texture.name);
				it->chunk = texture.chunk;
			}
			else
			{
				base.textures.push_back(texture);
			}
		}
	}

	uint32_t bodySize = 12 + 4;
	for (auto& texture : base.textures)
		bodySize += uint32_t(texture.chunk.size());

	if (base.extension.empty())
		bodySize += 12;
	else
		bodySize += uint32_t(base.extension.size());

	std::vector<uint8_t> out;
	out.reserve(12 + bodySize);
	WriteChunkHeader(out, RW_TEX_DICTIONARY_TYPE, bodySize, base.version);
	WriteChunkHeader(out, RW_STRUCT_TYPE, 4, base.version);

	uint32_t countAndDevice = uint32_t(base.textures.size() & 0xFFFF) | (uint32_t(base.deviceId) << 16);
	out.insert(out.end(), reinterpret_cast<uint8_t*>(&countAndDevice), reinterpret_cast<uint8_t*>(&countAndDevice) + 4);

	for (auto& texture : base.textures)
		out.insert(out.end(), texture.chunk.begin(), texture.chunk.end());

	if (base.extension.empty())
		WriteChunkHeader(out, RW_EXTENSION_TYPE, 0, base.version);
	else
		out.insert(out.end(), base.extension.begin(), base.extension.end());

	return out;
}

// PRS unpack, same as the game's Expand2. Control bits come one by one with low bit being first.
static bool DecompressPRS(const uint8_t* src, std::vector<uint8_t>& out, size_t maxSize)
{
	uint32_t ctrl = 0;
	int bits = 0;

	auto getBit = [&]()
		{
			if (bits == 0)
			{
				ctrl = *src++;
				bits = 8;
			}

			uint32_t bit = ctrl & 1;
			ctrl >>= 1;
			bits--;
			return bit;
		};

	while (true)
	{
		if (getBit())
		{
			if (out.size() >= maxSize)
				return false;

			out.push_back(*src++);
			continue;
		}

		ptrdiff_t offset;
		size_t size;

		if (getBit())
		{
			uint32_t value = src[0] | (src[1] << 8);
			src += 2;

			if (value == 0)
				return true;

			offset = ptrdiff_t(value >> 3) - 0x2000;
			size = value & 7;
			size = size == 0 ? size_t(*src++) + 1 : size + 2;
		}
		else
		{
			size = ((getBit() << 1) | getBit()) + 2;
			offset = ptrdiff_t(*src++) - 0x100;
		}

		if (ptrdiff_t(out.size()) + offset < 0 || out.size() + size > maxSize)
			return false;

		size_t from = out.size() + offset;

		for (size_t i = 0; i < size; i++)
			out.push_back(out[from + i]);
	}
}

static bool ReadTXDFile(const fs::path& path, std::vector<uint8_t>& data)
{
	std::ifstream file(path, std::ios::binary);

	if (!file)
		return false;

	data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return true;
}


static fs::path JoinModPath(const fs::path& dir, std::string value)
{
	std::replace(value.begin(), value.end(), '\\', '/');
	return (dir / fs::path(value)).lexically_normal();
}

static bool IsPathInModFolder(const fs::path& modDir, const fs::path& path)
{
	std::error_code curError;
	fs::path root = fs::weakly_canonical(modDir, curError);

	if (curError)
		return false;

	fs::path targetPath = fs::weakly_canonical(path, curError);

	if (curError)
		return false;

	fs::path relRoot = targetPath.lexically_relative(root);

	return !relRoot.empty() && *relRoot.begin() != "..";
}

// Append TXD works by adding "texture+=PathOfTheTxd" in the master ini file. Extra "," can be added to add multiple txds at once
// alternatively, user can specify a folder path and the TXD will append all the extra TXDs present at once.
static std::vector<fs::path> GetAppendTXDFiles(const fs::path& modDir, const fs::path& iniDir, const std::string& value, const std::string& modName)
{
	std::vector<fs::path> files;
	size_t start = 0;

	while (start <= value.size())
	{
		size_t comma = value.find(',', start);
		std::string text = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
		Trim(text);
		start = comma == std::string::npos ? value.size() + 1 : comma + 1;

		if (text.empty())
			continue;

		fs::path path = JoinModPath(iniDir, text);
		std::error_code ec;

		// file replacement that point outside mod directory should be ignored for safety
		if (!IsPathInModFolder(modDir, path)) 
		{
			continue;
		}

		if (fs::is_directory(path, ec)) // if user put a directory path, append all TXD files that exist in
		{
			std::vector<fs::path> folderFiles;

			for (auto& entry : fs::directory_iterator(path, ec))
			{
				if (entry.is_regular_file() && ToLower(entry.path().extension().string()) == ".txd" && IsPathInModFolder(modDir, entry.path()))
					folderFiles.push_back(entry.path());
			}

			std::sort(folderFiles.begin(), folderFiles.end());
			files.insert(files.end(), folderFiles.begin(), folderFiles.end());
		}
		else
		{
			files.push_back(std::move(path));
		}
	}

	return files;
}

static void AddIndividualFileReplacement(Mod* modinf, ModArchiveWorker rule)
{
	modinf->replaceRulesByAsset[HashLowerName(rule.asset)].push_back(static_cast<uint32_t>(modinf->archiveWorker.size()));
	modinf->archiveWorker.push_back(std::move(rule));
}

// Load master ini file that list all the individual files to replace
// This works with a key/value pair, where the key is the asset name and the value is the path to the replacement file.
static void LoadReplaceIni(const fs::path& modDir, const fs::path& iniPath, Mod* modinf, bool fromIncludeDir)
{
	if (!fs::exists(iniPath))
		return;

	IniFile ini(iniPath);
	fs::path iniDir = iniPath.parent_path();

	for (auto& [group, values] : ini)
	{
		std::string archive = group;
		Trim(archive);
		archive = NormalizeArchiveName(archive);

		for (auto& [key, data] : *values)
		{
			std::string asset = key;
			std::string value = data;
			Trim(asset);
			Trim(value);

			if (asset.empty() || asset[0] == '#')
				continue;

			if (archive.empty())
			{
				LOGF_ERROR("{}: {}: {} is not under an [archive] group", modinf->Name, iniPath.string(), asset);
				continue;
			}

			bool isWildcard = archive.find_first_of("*?") != std::string::npos;

			// the ini parser keeps the '+' of "+=" in the key
			if (asset.ends_with('+'))
			{
				asset.pop_back();
				Trim(asset);

				if (!ToLower(asset).ends_with(".txd"))
				{
					LOGF_ERROR("{}: [{}] {}+= only works on .txd files", modinf->Name, group, asset);
					continue;
				}

				ModArchiveWorker rule = { archive, ToLower(asset), {}, isWildcard, fromIncludeDir, true };

				for (auto& txdPath : GetAppendTXDFiles(modDir, iniDir, value, modinf->Name))
				{
					std::vector<uint8_t> data;
					ParsedTxdData txd;
					std::string error;

					if (!ReadTXDFile(txdPath, data))
					{
						LOGF_ERROR("{}: [{}] {}: file does not exist: {}", modinf->Name, group, asset, txdPath.string());
						continue;
					}

					if (!ParseTxd(data, txd, error))
					{
						LOGF_ERROR("{}: [{}] {}: {}: {}", modinf->Name, group, asset, txdPath.string(), error);
						continue;
					}

					// game txd version is set to RW 3.7.0.2
					// other versions may not load so warn player just in case
					if (txd.version != 0x1C020037)
						LOGF_WARNING("{}: {}: RW version {:#x} does not match the game (0x1c020037)", modinf->Name, txdPath.string(), txd.version);

					if (rule.file.empty())
						rule.file = txdPath;

					for (auto& texture : txd.textures)
						rule.textures.push_back(std::move(texture));
				}

				if (rule.textures.empty())
				{
					LOGF_ERROR("{}: [{}] {}+=: no texture to add", modinf->Name, group, asset);
					continue;
				}

				AddIndividualFileReplacement(modinf, std::move(rule));
				continue;
			}

			fs::path assetPath = JoinModPath(iniDir, value);

			if (value.empty() || !fs::is_regular_file(assetPath))
			{
				LOGF_ERROR("{}: [{}] {}: file does not exist: {}", modinf->Name, group, asset, assetPath.string());
				continue;
			}

			// file replacement that point outside mod directory should be ignored for safety
			if (!IsPathInModFolder(modDir, assetPath))
			{
				continue;
			}

			AddIndividualFileReplacement(modinf, { archive, ToLower(asset), assetPath, isWildcard, fromIncludeDir });
		}
	}
}

// --- Aura Color Helper for Mods START ---
//
// The trail from spin dash / homing attack and jump aura color are all hardcoded
// We will read the new colors from colors.ini to let mods change them.
static constexpr uint32_t SpinTrailColorTable = 0x449D60; // RGB is located at at +0x20 +0x24 and +0x28
static constexpr uint32_t SpinTrailColorStride = 0x2C;


// Each id is used to loads its own model file, the order below match the game one.
// The index is the game character id (stored at player+0x64) and the player always spawns as ID 14.
static constexpr const char* ColorCharacters[] =
{
	nullptr, "Sonic", "Tails", "Knuckles", "Amy", "Rouge", "Omega", "Vector", "Espio", "Maria",
	nullptr, nullptr, nullptr, "BossSonic", "Shadow",
	"2PYellow", "2PGunPink", "2PGunBlue", "2PCannonOrange", "2PCannonGreen",
};

using ModAuraColor = std::optional<std::array<uint8_t, 3>>;
static ModAuraColor g_spinTrailColors[std::size(ColorCharacters)];
static ModAuraColor g_jumpBallColors[std::size(ColorCharacters)];

// Table helper to find the charID used to spawn their jump ball, we look for the suffix directly
static constexpr std::pair<const char*, uint32_t> JumpBallSuffixes[] =
{
	{ "", 14 }, { "_S", 1 }, { "_T", 2 }, { "_K", 3 }, { "_A", 4 }, { "_R", 5 }, { "_O", 6 }, { "_V", 7 }, { "_E", 8 },
};

static thread_local int32_t g_curJumpBallID = -1;

template <size_t N>
static void LoadColorSection(const IniFile& ini, const char* section, const char* const (&names)[N], ModAuraColor(&colors)[N], const fs::path& iniPath, const std::string& modName)
{
	const IniGroup* group = ini.getGroup(section);
	
	if (!group)
		return;

	for (auto& [key, data] : *group->data())
	{
		std::string name = key;
		Trim(name);
		name = ToLower(name);

		auto it = std::find_if(std::begin(names), std::end(names), [&](const char* chara) { return chara && ToLower(chara) == name; });
		if (it == std::end(names))
		{
			LOGF_ERROR("{}: {}: [{}] unknown character {}", modName, iniPath.string(), section, key);
			continue;
		}

		int r, g, b;
		if (sscanf(data.c_str(), " %d , %d , %d", &r, &g, &b) != 3)
		{
			continue;
		}

		colors[it - std::begin(names)] = std::array<uint8_t, 3>{ (uint8_t)std::clamp(r, 0, 255), (uint8_t)std::clamp(g, 0, 255), (uint8_t)std::clamp(b, 0, 255) };
	}
}

static void LoadColorsIni(const fs::path& iniPath, const std::string& modName)
{
	if (!fs::exists(iniPath))
		return;

	IniFile ini(iniPath);
	LoadColorSection(ini, "SpinTrail", ColorCharacters, g_spinTrailColors, iniPath, modName);
	LoadColorSection(ini, "JumpBall", ColorCharacters, g_jumpBallColors, iniPath, modName);
}

// Player effect spawn
// Jump and trail aura use this function
X86_FUNC_IMPL(__imp__sub_8D1B0);
X86_OVERRIDE_FUNC(sub_8D1B0)
{
	uint32_t player = X86_LOAD_U32(X86_LOAD_U32(ctx.ecx.u32 + 0x28));
	uint32_t chara = player ? X86_LOAD_U32(player + 0x64) : UINT32_MAX;

	int32_t backupID = g_curJumpBallID;
	g_curJumpBallID = chara < std::size(ColorCharacters) ? static_cast<int32_t>(chara) : -1;
	__imp__sub_8D1B0(ctx, base);
	g_curJumpBallID = backupID;
}

// EffectObject ctor
// Memo: second param is the EFD: +0 = name, +0x38 = color A R G B.
X86_FUNC_IMPL(__imp__sub_38F810);
X86_OVERRIDE_FUNC(sub_38F810)
{
	uint32_t this_ = ctx.ecx.u32;
	uint32_t EFD = static_cast<uint32_t>(GetHostParameter(ctx, 2, CallConvention::Thiscall));

	__imp__sub_38F810(ctx, base);

	if (!EFD)
		return;

	auto recordName = reinterpret_cast<const char*>(g_memory.Translate(EFD));
	std::string_view name(recordName, strnlen(recordName, 32));

	// Name always start with "Jump_ball" we can safely give up if it's something else
	if (!name.starts_with("Jump_ball") || name.size() < 10)
		return;

	int32_t curCharID = g_curJumpBallID;
	if (curCharID < 0)
	{
		std::string_view suffix = name.substr(10);
		auto it = std::find_if(std::begin(JumpBallSuffixes), std::end(JumpBallSuffixes), [&](auto& s) { return suffix == s.first; });
		
		if (it == std::end(JumpBallSuffixes))
			return;

		curCharID = it->second;
	}

	if (!g_jumpBallColors[curCharID])
		return;

	// set new jump ball color
	X86_STORE_U32(this_ + 260, (*g_jumpBallColors[curCharID])[0] << 22);
	X86_STORE_U32(this_ + 264, (*g_jumpBallColors[curCharID])[1] << 22);
	X86_STORE_U32(this_ + 268, (*g_jumpBallColors[curCharID])[2] << 22);
}

void SetSpinTrailColors()
{
	auto base = g_memory.base;

	for (size_t i = 0; i < std::size(g_spinTrailColors); i++)
	{
		if (!g_spinTrailColors[i])
			continue;

		uint32_t row = SpinTrailColorTable + i * SpinTrailColorStride;
		X86_STORE_U32(row + 0x20, (*g_spinTrailColors[i])[0]);
		X86_STORE_U32(row + 0x24, (*g_spinTrailColors[i])[1]);
		X86_STORE_U32(row + 0x28, (*g_spinTrailColors[i])[2]);
	}
}

// --- Aura Color Helper for Mods END ---


void ModLoader::ApplyHelperPatches()
{
	SetSpinTrailColors();
}

static std::vector<std::string> ReadOneFileAssetNames(const fs::path& path)
{
	std::vector<std::string> names;
	std::ifstream file(path, std::ios::binary);
	char header[0xB0];

	if (!file.read(header, sizeof(header)))
		return names;

	uint32_t count = *reinterpret_cast<uint32_t*>(header + 0x1C);
	if (count > 0x4000)
		return names;

	std::vector<char> table(56 * count);
	if (!file.read(table.data(), table.size()))
		return names;

	for (uint32_t i = 0; i < count; i++)
		names.push_back(ToLower(std::string(table.data() + 56 * i, strnlen(table.data() + 56 * i, 44))));

	return names;
}


// Memo: AFS layout work by storing the entry names in a separate table at the end of the file, so we need to read the header and the table to get the names.
// Each name table record is 48 bytes and starts with name (32 bytes?).
static std::vector<std::string> GetAfsEntryNames(const fs::path& path)
{
	std::vector<std::string> names;
	std::ifstream afsFile(path, std::ios::binary);
	uint32_t afsHeader[2];

	if (!afsFile.read(reinterpret_cast<char*>(afsHeader), sizeof(afsHeader)) || afsHeader[0] != 0x00534641 || afsHeader[1] > 0x10000)
		return names;

	uint32_t table[2];
	afsFile.seekg(8 + 8 * size_t(afsHeader[1]));

	if (!afsFile.read(reinterpret_cast<char*>(table), sizeof(table)) || table[1] < 48 * afsHeader[1])
		return names;

	std::vector<char> records(48 * size_t(afsHeader[1]));
	afsFile.seekg(table[0]);

	if (!afsFile.read(records.data(), records.size()))
		return names;

	for (uint32_t i = 0; i < afsHeader[1]; i++)
		names.push_back(ToLower(std::string(records.data() + 48 * i, strnlen(records.data() + 48 * i, 32))));

	return names;
}


static void LogIndividualFilesReplacement()
{
	std::vector<std::pair<std::string, std::vector<std::string>>> archives;
	std::error_code ec;
	fs::path gameRoot = GetMainGameFolder();

	for (auto it = fs::recursive_directory_iterator(gameRoot, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
	{
		std::string ext = it->is_regular_file() ? ToLower(it->path().extension().string()) : std::string();

		if (ext != ".one" && ext != ".afs")
			continue;

		std::string archive = NormalizeArchiveName(fs::relative(it->path(), gameRoot).generic_string());
		bool isUsed = std::any_of(g_mods.begin(), g_mods.end(), [&](const Mod& mod)
			{
				return std::any_of(mod.archiveWorker.begin(), mod.archiveWorker.end(), [&](const ModArchiveWorker& modWK) { return isMatchingName(modWK, archive); });
			});

		if (!isUsed)
			continue;

		if (ext == ".afs")
		{
			g_afsEntryNames[archive] = GetAfsEntryNames(it->path());
			archives.emplace_back(std::move(archive), g_afsEntryNames[archive]);
		}
		else
		{
			archives.emplace_back(std::move(archive), ReadOneFileAssetNames(it->path()));
		}
	}

	for (auto& mod : g_mods)
	{
		for (auto& modwk : mod.archiveWorker)
		{
			uint32_t matchesCount = 0;
			for (auto& [archiveName, names] : archives)
			{
				if (isMatchingName(modwk, archiveName) && std::find(names.begin(), names.end(), modwk.asset) != names.end())
					matchesCount++;
			}

			const char* op = modwk.isAppend ? "+=" : "";

			if (matchesCount == 0)
				LOGF_IMPL(Error, "Mod Loader", "{}: [{}] {}{} matched no archive, check the names", mod.Name, modwk.archive, modwk.asset, op);
		}
	}
}

// archives loaded through LoadOneFileAsync (events) never fill their fname slot,
// so we keep their og name directly, using the TOneFile address as a key instead.
static std::mutex g_oneFileMutex;
static ankerl::unordered_dense::map<uint32_t, std::string> g_asyncOneFileNames;

// Files replacement are resolved once when the archive is loaded, so we can store them in a map for quick lookup later.
struct OneFileReplacement
{
	std::filesystem::path filePath;
	uint32_t size;
	std::shared_ptr<const std::vector<uint8_t>> data; // merged txd, served from memory instead of filePath
};
static ankerl::unordered_dense::map<uint32_t, xxHashMap<OneFileReplacement>> g_oneFileReplacements;
static bool flagIndividualFileReplace = false;

static std::string GetOneFileName(uint32_t oneFileAddr, TOneFile* oneFile)
{
	if (oneFile->fname[0])
		return NormalizeArchiveName(std::string(oneFile->fname, strnlen(oneFile->fname, sizeof(oneFile->fname))));

	std::lock_guard lock(g_oneFileMutex);
	auto it = g_asyncOneFileNames.find(oneFileAddr);
	return it != g_asyncOneFileNames.end() ? it->second : std::string();
}

static const ModArchiveWorker* FindReplacedFiles(const std::string& archive, XXH64_hash_t assetHash)
{
	for (int i = g_mods.size() - 1; i >= 0; i--) // iterate in reverse to prioritize later mods in the list (which are higher priority in mod loader)
	{
		auto ruleIt = g_mods[i].replaceRulesByAsset.find(assetHash);

		if (ruleIt == g_mods[i].replaceRulesByAsset.end())
			continue;

		const ModArchiveWorker* currentArchive = nullptr;

		for (uint32_t index : ruleIt->second)
		{
			auto& modWK = g_mods[i].archiveWorker[index];

			if (modWK.isAppend)
				continue;

			// if mod is replacing the same asset in multiple archives, pick the one with the highest priority
			if (isMatchingName(modWK, archive) && (!currentArchive || handleRulesPriority(modWK, *currentArchive)))
				currentArchive = &modWK;
		}

		if (currentArchive)
			return currentArchive;
	}

	return nullptr;
}


static std::vector<const ModArchiveWorker*> FindAppendedFiles(const std::string& archive, XXH64_hash_t assetHash)
{
	std::vector<const ModArchiveWorker*> resultWK;

	// lowest priority first so the strongest one is merged last and wins.
	for (auto& mod : g_mods)
	{
		auto ruleIt = mod.replaceRulesByAsset.find(assetHash);

		if (ruleIt == mod.replaceRulesByAsset.end())
			continue;

		size_t modStart = resultWK.size();

		for (uint32_t index : ruleIt->second)
		{
			auto& modWK = mod.archiveWorker[index];

			if (modWK.isAppend && isMatchingName(modWK, archive))
				resultWK.push_back(&modWK);
		}


		std::stable_sort(resultWK.begin() + modStart, resultWK.end(), [](auto a, auto b) { return handleRulesPriority(*b, *a); });
	}

	return resultWK;
}

// TOneFile::LoadOneFileAsync
X86_FUNC_IMPL(__imp__sub_1F7F0);
X86_OVERRIDE_FUNC(sub_1F7F0)
{
	if (auto name = (const char*)GetHostParameter(ctx, 2, CallConvention::Thiscall, ParameterType::Pointer))
	{
		std::lock_guard lock(g_oneFileMutex);
		g_asyncOneFileNames[ctx.ecx.u32] = NormalizeArchiveName(name);
	}

	__imp__sub_1F7F0(ctx, base);
}

// Memo: table record has the uncompressed size at +44, data pointer at +48 and flags at +52.
static std::shared_ptr<const std::vector<uint8_t>> MergeAppendedTxd(const std::string& archive, uint32_t record, const ModArchiveWorker* replaceWK, const std::vector<const ModArchiveWorker*>& appends)
{
	auto base = g_memory.base;
	std::vector<uint8_t> data;
	std::string baseName;

	if (replaceWK)
	{
		baseName = replaceWK->file.string();

		if (!ReadTXDFile(replaceWK->file, data))
			return nullptr;
	}
	else
	{
		baseName = appends.back()->asset;
		uint32_t size = X86_LOAD_U32(record + 96);
		auto src = reinterpret_cast<const uint8_t*>(g_memory.Translate(X86_LOAD_U32(record + 100)));

		if (X86_LOAD_U32(record + 104) & 1)
		{
			data.reserve(size);
			if (!DecompressPRS(src, data, size) || data.size() != size)
			{
				LOGF_IMPL(Error, "Mod Loader", "[{}] {}: could not unpack the original file", archive, baseName);
				return nullptr;
			}
		}
		else
		{
			data.assign(src, src + size);
		}
	}

	ParsedTxdData txd;
	std::string error;

	if (!ParseTxd(data, txd, error))
	{
		LOGF_IMPL(Error, "Mod Loader", "[{}] {}: can't add textures, {}", archive, baseName, error);
		return nullptr;
	}

	size_t ogCount = txd.textures.size();
	auto merged = std::make_shared<const std::vector<uint8_t>>(MergeTxd(txd, appends, archive));
	LOGF_IMPL(Utility, "Mod Loader", "[{}] {}: {} textures, {} added by mods", archive, baseName, txd.textures.size(), txd.textures.size() - ogCount);

	return merged;
}

// In order to implement individual file replacement in .one files, we need to patch the size of the asset table.
// since the original code will allocate a buffer based on the original content
// we have to patch each asset size that mods are replacing to have an accurate size and avoid memory overflow.
// the game will then later use this size for allocating memory properly. (This table fix up runs for both normal and
// async loads.)
// later on a separate function we patch the file loading function to load the replacement file instead of the original one.
X86_FUNC_IMPL(__imp__sub_1F360);
X86_OVERRIDE_FUNC(sub_1F360)
{
	uint32_t oneFileAddr = X86_LOAD_U32(ctx.esp.u32 + 4);
	__imp__sub_1F360(ctx, base);

	if (!flagIndividualFileReplace) // give up if no mods are replacing individual files
		return;

	TOneFile* oneFile = (TOneFile*)g_memory.Translate(oneFileAddr);
	std::string archive = GetOneFileName(oneFileAddr, oneFile);
	xxHashMap<OneFileReplacement> replacements;

	if (!archive.empty() && oneFile->assetStreamData)
	{
		uint32_t numAssets = X86_LOAD_U32(oneFile->assetStreamData + 16);

		for (uint32_t index = 2; index < numAssets + 2; index++)
		{
			uint32_t record = oneFile->assetStreamData + 56 * index;
			auto assetName = (const char*)g_memory.Translate(record + 52);
			XXH64_hash_t assetHash = HashLowerName(std::string(assetName, strnlen(assetName, 44))); // fixed 44-byte name field

			auto archiveWK = FindReplacedFiles(archive, assetHash);
			auto appends = FindAppendedFiles(archive, assetHash);

			if (!appends.empty())
			{
				auto mergedTXD = MergeAppendedTxd(archive, record, archiveWK, appends);
				if (mergedTXD)
				{
					X86_STORE_U32(record + 96, static_cast<uint32_t>(mergedTXD->size()));
					replacements[assetHash] = { archiveWK ? archiveWK->file : appends.back()->file, static_cast<uint32_t>(mergedTXD->size()), mergedTXD };
					continue;
				}
			}

			if (!archiveWK)
				continue;

			std::error_code ec;
			uintmax_t size = std::filesystem::file_size(archiveWK->file, ec);
			if (ec)
				continue;

			X86_STORE_U32(record + 96, static_cast<uint32_t>(size));
			replacements[assetHash] = { archiveWK->file, static_cast<uint32_t>(size) };
		}
	}

	// the TOneFile address gets reused by the next archive, so always overwrite what we had
	std::lock_guard lock(g_oneFileMutex);
	if (replacements.empty())
		g_oneFileReplacements.erase(oneFileAddr);
	else
		g_oneFileReplacements[oneFileAddr] = std::move(replacements);
}

// TOneFile::OpenData
// Now that we patched the asset table size, we can load the replacement file
// instead of the original one.
X86_FUNC_IMPL(__imp__sub_1F570);
X86_OVERRIDE_FUNC(sub_1F570)
{
	TOneFile* oneFile = (TOneFile*)GetHostParameter(ctx, 1, CallConvention::Thiscall, ParameterType::Pointer);
	auto assetName = oneFile ? TOneFileCheckFileName(oneFile, GetHostParameter(ctx, 2, CallConvention::Thiscall)) : nullptr;
	std::optional<OneFileReplacement> replacement;

	if (assetName)
	{
		XXH64_hash_t assetHash = HashLowerName(std::string(assetName, strnlen(assetName, 44)));

		std::lock_guard lock(g_oneFileMutex);
		auto oneIt = g_oneFileReplacements.find(ctx.ecx.u32);
		if (oneIt != g_oneFileReplacements.end())
		{
			auto assetIt = oneIt->second.find(assetHash);
			if (assetIt != oneIt->second.end())
				replacement = assetIt->second;
		}
	}

	auto dest = reinterpret_cast<char*>(GetHostParameter(ctx, 3, CallConvention::Thiscall, ParameterType::Pointer));

	if (replacement && replacement->data)
	{
		memcpy(dest, replacement->data->data(), replacement->size);
		ctx.eax.u32 = replacement->size;
		LOGF_IMPL(Utility, "Mod Loader", "Loading merged file: \"{}\"", std::string_view(assetName, strnlen(assetName, 44)));
	}
	else
	{
		std::ifstream file;

		if (replacement)
			file.open(replacement->filePath, std::ios::binary);

		if (!file.is_open())
		{
			__imp__sub_1F570(ctx, base);
			return;
		}

		file.read(dest, replacement->size);
		ctx.eax.u32 = static_cast<uint32_t>(file.gcount());
		LOGF_IMPL(Utility, "Mod Loader", "Loading file: \"{}\"", reinterpret_cast<const char*>(replacement->filePath.u8string().c_str()));
	}

	// async readers (events) pass a thread func and wait for a TOneFileNotice saying the data is ready
	if (uint32_t threadFunc = GetHostParameter(ctx, 4, CallConvention::Thiscall))
	{
		// TOneFileNotice::Create
		GuestToHostFunction<void, CallConvention::Cdecl>(ctx, sub_1F470, threadFunc, ctx.eax.u32, GetHostParameter(ctx, 5, CallConvention::Thiscall));
	}

	ctx.AddEsp(5); // we loaded the file ourselves, clean up the stack for the skipped function (4 args + ret addr)
}

// ADXT_StartAf(adxt, partition, entry)
X86_FUNC_IMPL(__imp__sub_32A9A0);
X86_OVERRIDE_FUNC(sub_32A9A0)
{
	uint32_t adxt = static_cast<uint32_t>(GetHostParameter(ctx, 1));
	uint32_t partition = static_cast<uint32_t>(GetHostParameter(ctx, 2));
	uint32_t entry = static_cast<uint32_t>(GetHostParameter(ctx, 3));

	// call OG if no mods are replacing anything
	if (!flagIndividualFileReplace || partition != 0)
	{
		__imp__sub_32A9A0(ctx, base);
		return;
	}

	// We first check which AFS is being used depending on voice language config, then we 
	// look up the entry name in the table we built earlier.
	bool isEnglish = X86_LOAD_U32(0x54DD8C) != 0;
	const char* afsName = (const char*)(g_memory.Translate(X86_LOAD_U32(isEnglish ? 0x51DDA0 : 0x51DD9C)));
	std::string archive = NormalizeArchiveName(afsName);
	const ModArchiveWorker* modWK = nullptr;
	std::string key = "";

	auto namesIt = g_afsEntryNames.find(archive);
	if (namesIt != g_afsEntryNames.end() && entry < namesIt->second.size())
	{
		key = archive + "/" + namesIt->second[entry];
		modWK = FindReplacedFiles(archive, HashLowerName(namesIt->second[entry]));
	}

	if (!modWK)
	{
		__imp__sub_32A9A0(ctx, base);
		return;
	}

	{
		std::lock_guard lock(g_afsMutex);
		g_afsReplacedFiles[key] = modWK->file;
	}

	// ADXT_StartFname copy the name, so a string under the guest stack is enough
	X86Context nameCtx = ctx;
	nameCtx.esp.u32 -= 0x110;
	strncpy(reinterpret_cast<char*>(g_memory.Translate(nameCtx.esp.u32)), key.c_str(), 0xFF);
	X86_STORE_U8(nameCtx.esp.u32 + 0xFF, 0);

	//Call ADXT_StartFname
	GuestToHostFunction<void>(nameCtx, sub_32A9D0, adxt, nameCtx.esp.u32);
	ctx.AddEsp(1);
}


void ModLoader::Init()
{
	using json = nlohmann::json;
	const std::filesystem::path& userPath = GetUserPath();

	const std::filesystem::path& gPath = GetGamePath();

	std::filesystem::path profileListPath = gPath / "mods/.modloader/profiles/profiles.json";
	std::filesystem::path defaultProfile = gPath / "mods/.modloader/profiles/Default.json";

	std::filesystem::path currentProfile = defaultProfile;

	int ind_profile = -1;
	if (std::filesystem::exists(profileListPath))
	{
		std::ifstream ifs(profileListPath);
		json json_profiles = json::parse(ifs);
		ifs.close();

		ind_profile = json_profiles.value("ProfileIndex", 0);
		json proflist = json_profiles["ProfilesList"];
		std::string profname = proflist.at(ind_profile)["Filename"].get<std::string>();
		currentProfile = gPath / "mods/.modloader/profiles" / profname;
	}


	if (ind_profile == -1 && std::filesystem::exists(defaultProfile) == false)
	{
		return; // give up if no profile is found
	}

	std::ifstream ifs(currentProfile);

	json json_profile;
	json_profile = json::parse(ifs);
	ifs.close();

	json json_mods = json_profile["EnabledMods"];
	for (unsigned int i = 1; i <= json_mods.size(); i++)
	{
		std::string mod_fname = json_mods.at(i - 1);


		std::filesystem::path modDir = gPath / "mods" / mod_fname;

		if (!std::filesystem::exists(modDir))
			continue;

		std::filesystem::path modIni = modDir / "mod.ini";

		if (!std::filesystem::exists(modIni))
			continue;

		std::unique_ptr<IniFile> ini_mod(new IniFile(modIni));
		const IniGroup* const modinfo = ini_mod->getGroup("");

		const std::string mod_nameA = modinfo->getString("Name");
		LOGF_IMPL(Utility, "Mod Loader", "Loading: \"{}\"", reinterpret_cast<const char*>(mod_nameA.c_str()));

		Mod modinf = {
		mod_nameA,
		modinfo->getString("Author"),
		modinfo->getString("Description"),
		modinfo->getString("Version"),
		"",
		modinfo->getString("ModID"),
		modinfo->getBool("RedirectMainSave"),
		};

		std::filesystem::path modGameDir = JoinModPath(modDir, "game");

		bool commonDirExist = false;
		bool includeDirExist = false;

		if (std::filesystem::exists(modGameDir))
		{
			modinf.commonDir = modGameDir;
			commonDirExist = true;
		}

		if (ini_mod->hasGroup("Config"))
		{
			int dirCount = ini_mod->getInt("Config", "IncludeDirCount", 0);
			if (dirCount > 0)
			{
				std::filesystem::path configPath = modDir / "config.ini";
				std::unique_ptr<IniFile> configini = std::filesystem::exists(configPath) ? std::make_unique<IniFile>(configPath) : nullptr;

				for (uint16_t d = 0; d < dirCount; d++)
				{
					// Retrieves defaults from mod.ini
					std::string dirStr = "IncludeDir" + std::to_string(d);
					std::string incDirPath = ini_mod->getString("Config", dirStr);

					// Loop through all groups in the config ini to check if the current IncludeDir key is present.
					if (configini)
					{

						for (auto group = configini->begin(); group != configini->end(); group++)
						{
							// If the corresponding IncludeDir key is found, update the paths with the config ini info.
							if (group->second->hasKey(dirStr))
							{
								incDirPath = group->second->getString(dirStr);
								break;
							}
						}
					}


					if (incDirPath.empty())
						continue;

					if (!IsPathInModFolder(modDir, JoinModPath(modDir, incDirPath)))
					{
						continue;
					}

					LOGF_IMPL(Utility, "Mod Loader", "Mod {} use config folder {}", reinterpret_cast<const char*>(mod_nameA.c_str()), reinterpret_cast<const char*>(incDirPath.c_str()));
					includeDirExist = true;

					std::filesystem::path modConfigGameDir = JoinModPath(modDir, incDirPath + "/game");
					modinf.includeDirs.push_back(modConfigGameDir);
				}
			}
		}


		if (modinf.MainSaveRedirect)
		{
			s_saveFilePath = modDir / "save" / "Save.dat";
			LOGF_IMPL(Utility, "Mod Loader", "Mod {} redirects the save to {}", reinterpret_cast<const char*>(mod_nameA.c_str()), reinterpret_cast<const char*>(s_saveFilePath.parent_path().u8string().c_str()));
		}

		if (strcmp(reservedIDName[(uint8_t)ModReservedID::Reloaded], modinf.ID.c_str()) == 0)
		{
			Config::ReloadedMod.Value = true;
		}

		if (strcmp(reservedIDName[(uint8_t)ModReservedID::Multiplayer], modinf.ID.c_str()) == 0)
		{
			Config::ExtendedMultiplayer.Value = true;
		}

		LoadReplaceIni(modDir, modDir / "replace.ini", &modinf, false);

		// for each codeless config folder, load its replace.ini if it exists
		for (auto& includeDir : modinf.includeDirs)
		{
			LoadReplaceIni(modDir, includeDir.parent_path() / "replace.ini", &modinf, true); // next to the folder's "game" dir
		}


		LoadColorsIni(modDir / "colors.ini", mod_nameA);
		for (auto& includeDir : modinf.includeDirs)
		{
			LoadColorsIni(includeDir.parent_path() / "colors.ini", mod_nameA);
		}

		if (commonDirExist || includeDirExist || !modinf.archiveWorker.empty())
		{
			flagIndividualFileReplace |= !modinf.archiveWorker.empty();
			g_mods.push_back(std::move(modinf));
		}
	}

	if (flagIndividualFileReplace)
		LogIndividualFilesReplacement();


	// --- load codes if any ---

	if (!json_profile.contains("EnabledCodes") || !json_profile["EnabledCodes"].is_array())
		return;

	// the json profile stores the config key of each code so we can compare it directly with hidden code defs
	std::unordered_map<std::string_view, IConfigDef*> codeDefs;
	for (auto& def : g_configDefinitions)
	{
		if (def->IsHidden() && def->GetSection() == "Codes")
			codeDefs.emplace(def->GetName(), def);
	}

	for (auto& entry : json_profile["EnabledCodes"])
	{
		if (!entry.is_string())
			continue;

		std::string curCode = entry.get<std::string>();
		auto it = codeDefs.find(curCode);


		if (it == codeDefs.end())
		{
			LOGF_IMPL(Utility, "Mod Loader", "Unknown code: \"{}\"", curCode);
			continue;
		}

		LOGF_IMPL(Utility, "Mod Loader", "Loading code: \"{}\"", curCode);
		*(bool*)it->second->GetValue() = true;
	}
}
