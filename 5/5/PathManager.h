#ifndef PATH_MANAGER_HPP
#define PATH_MANAGER_HPP

#include <windows.h>
#include <algorithm>
#include <string>

namespace PathManager
{
	inline std::wstring FindDataRoot()
	{
		wchar_t buffer[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, buffer, MAX_PATH);

		std::wstring dir = buffer;
		std::replace(dir.begin(), dir.end(), L'\\', L'/');
		dir = dir.substr(0, dir.find_last_of(L'/'));

		for (int i = 0; i < 6; i++)
		{
			DWORD attr = GetFileAttributesW((dir + L"/Shaders").c_str());
			if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
			{
				return dir + L"/";
			}

			size_t slash = dir.find_last_of(L'/');
			if (slash == std::wstring::npos) { break; }
			dir = dir.substr(0, slash);
		}

		MessageBoxW(nullptr, L"Data folder (Shaders, Models, Textures...) not found next to the exe.", L"Missing data", MB_ICONERROR);
		ExitProcess(1);
	}

	inline std::string ToUtf8(const std::wstring& w)
	{
		int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(size, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), size, nullptr, nullptr);
		return s;
	}
}

inline const std::wstring LOCAL_PATH_W = PathManager::FindDataRoot();
inline const std::string LOCAL_PATH = PathManager::ToUtf8(LOCAL_PATH_W);

const std::string POST_PROCESS_FOLDER = "PostProcess/";
const std::wstring POST_PROCESS_FOLDER_W = L"PostProcess/";

const std::string TERRAIN_FOLDER = "Terrain/";
const std::wstring TERRAIN_FOLDER_W = L"Terrain/";

#endif // !PATH_MANAGER_HPP
