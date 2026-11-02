// as_vip — the engine-free part of the module: where the "VIP" table is read from, the
// self-contained Valve KeyValues (KV1) reader that parses it, and the group selection of the
// original's OnAdminConnected.
//
// Self-contained on purpose (docs/PLUGIN_STANDARD.md §7: no pisexcompat, no helper library). The
// module is a pure consumer of the admin_system and vip Plugify APIs and makes no s2sdk calls, so
// the only thing it needs from the host is the configs directory. Header-only, so
// tests/as_vip_tests.cpp exercises exactly the code the plugin runs, on the shipped vip.ini files.
//
// 1. The file. The production config is the Metamod installation's
//    addons/configs/admin_system/vip.ini, unchanged (PLUGIN_STANDARD §7: production KeyValues files
//    are read as they are, no conversion):
//
//        "VIP"
//        {
//            "@admin/root"       "PROVIP"
//            "@admin/megavip"    "MEGA"
//        }
//
//    ConfigCandidates() lists where it is looked up, in order: <configs>/admin_system/vip.ini (the
//    directory every admin module shares, i.e. the Pisex layout) and then <configs>/as_vip/vip.ini
//    (the plugin's own folder, PLUGIN_STANDARD §3). The caller supplies the configs directory
//    (plg::GetConfigsDir() in src/plugin.cpp); this header never touches the Plugify API.
//
// 2. LoadKv1File() / ParseKv1(): a read-only Valve KeyValues (KV1) reader. The accepted grammar is
//    Valve's: quoted ("key" "value") and unquoted (key value) tokens, a key may carry a value and a
//    block, nested blocks, // and /* */ comments, \" \\ \n \r \t escapes inside quotes, and
//    #base / #include / #define lines are skipped instead of followed. A malformed file - an
//    unterminated string, a missing '}' or a stray top-level '}' - is rejected as a whole
//    (std::nullopt), so half a table never grants the wrong group. A UTF-8 BOM is ignored.
//
// 3. LoadGroupMapFromFile(): LoadConfig() of the original - FOR_EACH_VALUE over the "VIP" block,
//    i.e. one permission -> vip group entry per child that carries a value, in file order (the map
//    itself is std::map, so SelectGroup walks it lexicographically, exactly like the original's
//    g_mapGroups). The table is the block named "VIP" (case-insensitive, like Valve's FindKey); a
//    file without it is rejected instead of being merged wholesale.

#pragma once

#include <cctype>
#include <cstddef>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace as_vip {

// g_mapGroups of the original. std::map on purpose: the non-root branch of OnAdminConnected
// walks the entries in map order (lexicographic by permission string), and the port keeps it.
using GroupMap = std::map<std::string, std::string>;
using PermissionCheck = std::function<bool(const std::string&)>;

// --- 1. the KV1 reader ------------------------------------------------------------------------

struct KvNode {
	std::string name;
	std::string value;
	bool hasValue = false;
	bool isBlock = false;
	std::vector<KvNode> children;

	// First child with that name (case-insensitive, like Valve's FindKey), or nullptr.
	const KvNode* Find(std::string_view key) const;
};

namespace detail {

inline char LowerCase(char c) {
	return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline bool EqualNoCase(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (LowerCase(a[i]) != LowerCase(b[i])) {
			return false;
		}
	}
	return true;
}

//   file  := node* EOF
//   node  := token [ token ] [ '{' node* '}' ]
//   token := '"' escaped '"' | raw
class Parser {
public:
	explicit Parser(std::string_view text) : text_(text) {}

	bool Parse(KvNode& root) {
		ParseNodes(root, false);
		return !failed_;
	}

private:
	void SkipSpace() {
		while (pos_ < text_.size()) {
			const char c = text_[pos_];
			if (std::isspace(static_cast<unsigned char>(c))) {
				++pos_;
				continue;
			}
			if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
				pos_ += 2;
				while (pos_ < text_.size() && text_[pos_] != '\n') {
					++pos_;
				}
				continue;
			}
			if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
				pos_ += 2;
				while (pos_ + 1 < text_.size() && !(text_[pos_] == '*' && text_[pos_ + 1] == '/')) {
					++pos_;
				}
				pos_ = (pos_ + 1 < text_.size()) ? pos_ + 2 : text_.size();
				continue;
			}
			break;
		}
	}

	// One token; false at the end of the input or at a '}'.
	bool ReadToken(std::string& out) {
		SkipSpace();
		if (pos_ >= text_.size() || text_[pos_] == '}') {
			return false;
		}
		out.clear();
		if (text_[pos_] == '"') {
			++pos_;
			while (pos_ < text_.size()) {
				const char c = text_[pos_++];
				if (c == '\\' && pos_ < text_.size()) {
					const char esc = text_[pos_++];
					switch (esc) {
					case 'n':
						out.push_back('\n');
						break;
					case 'r':
						out.push_back('\r');
						break;
					case 't':
						out.push_back('\t');
						break;
					case '"':
						out.push_back('"');
						break;
					case '\\':
						out.push_back('\\');
						break;
					default:
						out.push_back('\\');
						out.push_back(esc);
						break;
					}
					continue;
				}
				if (c == '"') {
					return true;
				}
				out.push_back(c);
			}
			failed_ = true; // unterminated string
			return true;
		}
		while (pos_ < text_.size()) {
			const char c = text_[pos_];
			if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}' || c == '"') {
				break;
			}
			if (c == '/' && pos_ + 1 < text_.size() && (text_[pos_ + 1] == '/' || text_[pos_ + 1] == '*')) {
				break;
			}
			out.push_back(c);
			++pos_;
		}
		return true;
	}

	bool PeekIs(char c) {
		SkipSpace();
		return pos_ < text_.size() && text_[pos_] == c;
	}

	// Children are appended as soon as they are complete, so the recursion below works on a local
	// node and never holds a reference into a vector that the recursion may reallocate.
	void ParseNodes(KvNode& parent, bool insideBlock) {
		while (true) {
			SkipSpace();
			if (pos_ >= text_.size()) {
				if (insideBlock) {
					failed_ = true; // missing '}'
				}
				return;
			}
			if (text_[pos_] == '}') {
				if (!insideBlock) {
					failed_ = true; // stray '}'
				}
				++pos_;
				return;
			}
			std::string key;
			if (!ReadToken(key)) {
				return;
			}
			if (!key.empty() && key[0] == '#') {
				// #base / #include / #define: no inclusion, skip the line.
				while (pos_ < text_.size() && text_[pos_] != '\n') {
					++pos_;
				}
				continue;
			}
			KvNode node;
			node.name = std::move(key);
			if (!PeekIs('{')) {
				std::string value;
				if (ReadToken(value)) {
					node.value = std::move(value);
					node.hasValue = true;
				}
			}
			if (PeekIs('{')) {
				++pos_;
				node.isBlock = true;
				ParseNodes(node, true);
			}
			parent.children.push_back(std::move(node));
		}
	}

	std::string_view text_;
	std::size_t pos_ = 0;
	bool failed_ = false;
};

} // namespace detail

inline const KvNode* KvNode::Find(std::string_view key) const {
	if (key.empty()) {
		return nullptr;
	}
	for (const KvNode& child : children) {
		if (detail::EqualNoCase(child.name, key)) {
			return &child;
		}
	}
	return nullptr;
}

// The file root: an unnamed node whose children are the top-level entries. Kept that way - the
// "VIP" table is then found by name, exactly as a human reads the file.
inline std::optional<KvNode> ParseKv1(std::string_view text) {
	// A UTF-8 BOM would otherwise become part of the first key ("VIP") and hide the table from the
	// lookup below, silently emptying the map.
	constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
	if (text.starts_with(kUtf8Bom)) {
		text.remove_prefix(kUtf8Bom.size());
	}
	KvNode root;
	detail::Parser parser(text);
	if (!parser.Parse(root)) {
		return std::nullopt;
	}
	return root;
}

inline std::optional<KvNode> LoadKv1File(const std::string& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return std::nullopt;
	}
	std::ostringstream content;
	content << file.rdbuf();
	return ParseKv1(content.str());
}

// --- 2. where the table is read from ----------------------------------------------------------

// The admin_system directory every Pisex admin module shares; production vip.ini lives there.
inline constexpr std::string_view kAdminSystemDir = "admin_system";
// The plugin's own folder (PLUGIN_STANDARD §3: the folder name matches the manifest name).
inline constexpr std::string_view kPluginDir = "as_vip";
inline constexpr std::string_view kConfigFile = "vip.ini";

inline std::string JoinPath(std::string_view base, std::string_view name) {
	if (base.empty()) {
		return std::string(name);
	}
	if (name.empty()) {
		return std::string(base);
	}
	std::string out(base);
	if (out.back() != '/') {
		out.push_back('/');
	}
	if (name.front() == '/') {
		name.remove_prefix(1);
	}
	out.append(name);
	return out;
}

// <configs>/admin_system/vip.ini, then <configs>/as_vip/vip.ini. Empty when the host reported no
// configs directory (plg::GetConfigsDir is filled by the plugify module at load).
inline std::vector<std::string> ConfigCandidates(std::string_view configsDir) {
	std::vector<std::string> candidates;
	if (configsDir.empty()) {
		return candidates;
	}
	candidates.reserve(2);
	candidates.push_back(JoinPath(JoinPath(configsDir, kAdminSystemDir), kConfigFile));
	candidates.push_back(JoinPath(JoinPath(configsDir, kPluginDir), kConfigFile));
	return candidates;
}

inline bool FileExists(const std::string& path) {
	if (path.empty()) {
		return false;
	}
	std::ifstream file(path, std::ios::binary);
	return file.good();
}

// --- 3. the "VIP" table -----------------------------------------------------------------------

// The "VIP" block of a parsed file, or nullptr when the file has no such block (a plain key of
// that name is not a table). Case-insensitive, like Valve's FindKey.
inline const KvNode* FindVipBlock(const KvNode& fileRoot) {
	const KvNode* vip = fileRoot.Find("VIP");
	return (vip != nullptr && vip->isBlock) ? vip : nullptr;
}

// LoadConfig() of the original: FOR_EACH_VALUE over the KeyValues loaded from vip.ini. Only
// children that carry a value are permission entries - a nested block without a value is a
// KeyValues sub-key and contributes nothing, exactly like the original's GetFirstValue loop.
inline void LoadGroupMap(const KvNode& vipBlock, GroupMap& out) {
	for (const KvNode& entry : vipBlock.children) {
		if (entry.hasValue) {
			out[entry.name] = entry.value;
		}
	}
}

namespace detail {

inline bool LoadGroupMapFromRoot(const KvNode& fileRoot, GroupMap& out) {
	const KvNode* vipBlock = FindVipBlock(fileRoot);
	if (vipBlock == nullptr) {
		return false;
	}
	// Fill a copy: a caller that tries the next candidate keeps whatever it had on failure.
	GroupMap loaded;
	LoadGroupMap(*vipBlock, loaded);
	out = std::move(loaded);
	return true;
}

} // namespace detail

// Parse `text` and replace `out` with its "VIP" table. False (and `out` untouched) when the text is
// malformed or has no "VIP" block.
inline bool LoadGroupMapFromText(std::string_view text, GroupMap& out) {
	const std::optional<KvNode> fileRoot = ParseKv1(text);
	if (!fileRoot) {
		return false;
	}
	return detail::LoadGroupMapFromRoot(*fileRoot, out);
}

// Same over a file; false when the file is missing, malformed or has no "VIP" block.
inline bool LoadGroupMapFromFile(const std::string& path, GroupMap& out) {
	const std::optional<KvNode> fileRoot = LoadKv1File(path);
	if (!fileRoot) {
		return false;
	}
	return detail::LoadGroupMapFromRoot(*fileRoot, out);
}

// --- 4. the selection -------------------------------------------------------------------------

// OnAdminConnected() of the original with the permission lookups passed in: an admin holding
// "@admin/root" gets that entry of the map when it exists, every other admin gets the first
// entry whose permission he holds.
//
// (The is-root test is computed with the same callback, so it and the loop see exactly the
// permissions HasPermission reports.)
//
// Consequences kept:
//   * a root admin without an "@admin/root" entry in vip.ini gets *nothing* - the else-branch
//     does not run for root admins;
//   * the mapped group is passed through unchanged, including an empty one ("@admin/root" "").
// Returns the group to grant, or nullptr when the admin gets none.
inline const std::string* SelectGroup(const GroupMap& groups, const PermissionCheck& hasPermission) {
	if (hasPermission("@admin/root")) {
		const auto it = groups.find("@admin/root");
		return it != groups.end() ? &it->second : nullptr;
	}
	for (const auto& entry : groups) {
		if (hasPermission(entry.first)) {
			return &entry.second;
		}
	}
	return nullptr;
}

} // namespace as_vip
