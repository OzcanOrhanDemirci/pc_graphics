// The language of everything on screen: Turkish (the default) or English. Every text is written in both languages where
// it is used, L("Türkçe", "English"), so that a new text cannot exist in one only. The choice is remembered in user.cfg
// at the project root (git ignores it) and can be given on the command line (--lang tr|en). The interface kit's capitals
// (Turkish: i -> İ) and the numbers' decimal separator follow it.
#pragma once
#include <string>

namespace app {

enum class Lang { TR, EN };
Lang lang();
void setLang(Lang l);

inline const char* L(const char* tr, const char* en) { return lang() == Lang::EN ? en : tr; }
inline std::string L(const std::string& tr, const std::string& en) { return lang() == Lang::EN ? en : tr; }

Lang loadLang(const std::string& root);           // user.cfg's "lang", Turkish when there is none
void saveLang(const std::string& root, Lang l);   // user.cfg keeps its other lines

}   // namespace app
