#pragma once

#include "simulation_compare.h"
#include "simulation_xml.h"

#include <windows.h>

#include <filesystem>

namespace physics_test
{
    // The models folder is copied next to the test DLL by the build.
    inline std::filesystem::path models_dir()
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&models_dir), &module);
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(module, path, MAX_PATH);
        return std::filesystem::path(path).parent_path() / "models";
    }

    inline loaded_simulation load_model_file(const std::filesystem::path& path, std::string* error_out = nullptr)
    {
        loaded_simulation loaded;
        std::string error;
        loaded.sim = load_simulation_xml(path.wstring(), loaded.masses, loaded.springs, loaded.dampers, error);
        if (error_out)
            *error_out = error;
        else if (!loaded.sim)
            Assert::Fail(std::wstring(error.begin(), error.end()).c_str());
        return loaded;
    }

    inline loaded_simulation load_model(int number)
    {
        return load_model_file(models_dir() / ("simulation" + std::to_string(number) + ".xml"));
    }
}
