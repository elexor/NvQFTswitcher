#include "config.h"
#include <sys/stat.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cstdlib>

std::filesystem::path getConfigPath()
{
    char* appdataDir = nullptr;
    size_t size;
    errno_t err = _dupenv_s(&appdataDir, &size, "APPDATA");
    if (err != 0 || appdataDir == nullptr)
    {
        std::cerr << "Could not get APPDATA directory" << std::endl;
        exit(1);
    }

    std::filesystem::path configDir = std::filesystem::path(appdataDir) / "nvQFTswitcher";

    if (!std::filesystem::exists(configDir))
    {
        std::filesystem::create_directory(configDir);
    }

    free(appdataDir);
    return configDir;
}

std::vector<savedMode> loadModes()
{
    std::vector<savedMode> modes;

    std::string configPath = getConfigPath().string() + "\\saved_modes.bin";

    std::ifstream infile(configPath, std::ios::binary);

    if (infile.good())
    {
        size_t numModes;
        infile.read(reinterpret_cast<char*>(&numModes), sizeof(numModes));

        for (size_t i = 0; i < numModes; ++i)
        {
            savedMode saved;
            infile.read(reinterpret_cast<char*>(&saved), sizeof(saved));
            modes.push_back(saved);
        }

        infile.close();
    }
    else
    {
        // Handle error opening file for reading
    }
    return modes;
}

void saveMode(const savedMode& mode)
{
    std::string configPath = getConfigPath().string() + "\\saved_modes.bin";

    std::ifstream infile(configPath, std::ios::binary);

    std::vector<savedMode> modes;
    bool alreadyExists = false;

    if (infile.good())
    {
        size_t numModes;
        infile.read(reinterpret_cast<char*>(&numModes), sizeof(numModes));

        for (size_t i = 0; i < numModes; ++i)
        {
            savedMode saved;
            infile.read(reinterpret_cast<char*>(&saved), sizeof(saved));
            modes.push_back(saved);

            if (saved.dispId == mode.dispId && saved.refreshrate == mode.refreshrate)
            {
                alreadyExists = true;
            }
        }

        infile.close();
    }

    if (!alreadyExists)
    {
        modes.push_back(mode);

        std::ofstream outfile(configPath, std::ios::binary);

        if (outfile.good())
        {
            size_t numModes = modes.size();
      
      outfile.write(reinterpret_cast<const char*>(&numModes), sizeof(numModes));

            for (const savedMode& saved : modes)
            {
                outfile.write(reinterpret_cast<const char*>(&saved), sizeof(saved));
            }

            outfile.close();
        }
        else
        {
            // Handle error opening file for writing
        }
    }
}

void deleteMode(const savedMode& mode)
{
    std::string configPath = getConfigPath().string() + "\\saved_modes.bin";
    std::fstream file(configPath, std::ios::in | std::ios::out | std::ios::binary);

    std::vector<savedMode> modes;

    if (file.good())
    {
        size_t numModes;
        file.read(reinterpret_cast<char*>(&numModes), sizeof(numModes));

        for (size_t i = 0; i < numModes; ++i)
        {
            savedMode saved;
            file.read(reinterpret_cast<char*>(&saved), sizeof(saved));

            if (saved.dispId != mode.dispId || saved.refreshrate != mode.refreshrate)
            {
                modes.push_back(saved);
            }
        }

        file.seekp(0);

        numModes = modes.size();
        file.write(reinterpret_cast<const char*>(&numModes), sizeof(numModes));

        for (const savedMode& saved : modes)
        {
            file.write(reinterpret_cast<const char*>(&saved), sizeof(saved));
        }

        std::ofstream outfile(configPath, std::ios::binary | std::ios::trunc);
        outfile.close();
    }
}

void save_baseMode(baseMode mode) {

    std::string configPath = getConfigPath().string() + "\\basemodes.bin";

    std::fstream file(configPath, std::ios::in | std::ios::out | std::ios::binary);

    // Check if file exists
    if (!file) {
        // Create a new file if it doesn't exist
        file.open(configPath, std::ios::out | std::ios::binary);
    }
    else {
        // Find the position of the existing mode with the same dispId (if any)
        file.seekg(0, std::ios::beg);
        std::vector<baseMode> modes;
        baseMode temp;
        while (file.read(reinterpret_cast<char*>(&temp), sizeof(temp))) {
            if (temp.dispId != mode.dispId) {
                modes.push_back(temp);
            }
        }

        // Rewind the file to overwrite the existing mode
        file.clear();
        file.seekp(0, std::ios::beg);

        // Write all the modes (except the one with the same dispId)
        for (const auto& m : modes) {
            file.write(reinterpret_cast<const char*>(&m), sizeof(m));
        }
    }

    // Write the new mode to the end of the file
    file.seekp(0, std::ios::end);
    file.write(reinterpret_cast<const char*>(&mode), sizeof(mode));
    file.close();
}

baseMode load_baseMode(int dispId) {

    std::string configPath = getConfigPath().string() + "\\basemodes.bin";

    std::fstream file(configPath, std::ios::in | std::ios::binary);
    baseMode mode;

    // Find the mode with the given dispId
    while (file.read(reinterpret_cast<char*>(&mode), sizeof(mode))) {
        if (mode.dispId == dispId) {
            file.close();
            return mode;
        }
    }

    file.close();
    // Return an empty mode if the dispId wasn't found
    return {};
}
