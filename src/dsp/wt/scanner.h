/* Wavetable scanner -- the table list (the mN_select values): directory listings only, no audio decode.
 *   0             "Init" (the built-in table, always there)
 *   then          <module>/wavetables/** (.wt2048 FLAC and .wav; the shipped packs, and any the user adds in a
 *                 folder of their own), sorted by "Pack/Name"
 */
#pragma once

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace dc {

struct WtEntry {
    std::string name;     /* shown in the enum list: "Pack/Name" or "Name" */
    std::string path;     /* absolute; empty for the built-in Init         */
    int flacFrameSize = 0;/* >0: frame size named by the .wtNNNN extension  */
    bool flac = false;    /* a .wt* file: FLAC, whatever the number says    */
};

class WtScanner {
public:
    void scan(const std::string &moduleDir)
    {
        entries.clear();
        entries.push_back({ "Init", "", 0 });

        scanDir(moduleDir + "/wavetables", "", 0);

        /* keep Init first, sort the rest by name */
        std::sort(entries.begin() + 1, entries.end(),
                  [](const WtEntry &a, const WtEntry &b) { return a.name < b.name; });
    }

    const std::vector<WtEntry> &list() const { return entries; }

    int indexOfName(const char *name) const
    {
        for (size_t i = 0; i < entries.size(); i++)
            if (entries[i].name == name)
                return (int) i;
        return -1;
    }

private:
    void scanDir(const std::string &dir, const std::string &prefix, int depth)
    {
        if (depth > 3) return;                     /* sanity bound */
        DIR *d = ::opendir(dir.c_str());
        if (!d) return;

        while (dirent *e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string full = dir + "/" + e->d_name;

            struct stat st;
            if (::stat(full.c_str(), &st) != 0) continue;

            if (S_ISDIR(st.st_mode)) {
                scanDir(full, prefix.empty() ? std::string(e->d_name)
                                             : prefix + "/" + e->d_name, depth + 1);
                continue;
            }

            const char *dot = std::strrchr(e->d_name, '.');
            if (!dot) continue;

            int flacSize = 0;
            bool isFlac = false;
            if (!strcasecmp(dot, ".wav")) {
                /* plain WAV */
            } else if (!strncasecmp(dot, ".wt", 3)) {
                /* .wtNNNN is FLAC and NAMES its frame size. Take the number
                 * when it is one wtBuild can use (32..4096, power of two) and
                 * otherwise still list the file -- the loader infers the size
                 * from the decoded length, the same as it does for a WAV.
                 * The floor used to be 256, so a .wt64 was skipped entirely
                 * and the file was invisible rather than merely unlabelled. */
                flacSize = std::atoi(dot + 3);
                if (flacSize < 32 || flacSize > 4096 ||
                    (flacSize & (flacSize - 1)) != 0)
                    flacSize = 0;
                isFlac = true;
            } else {
                continue;
            }

            std::string base(e->d_name, (size_t) (dot - e->d_name));
            entries.push_back({ prefix.empty() ? base : prefix + "/" + base,
                                full, flacSize, isFlac });
        }
        ::closedir(d);
    }

    std::vector<WtEntry> entries;
};

} // namespace dc
