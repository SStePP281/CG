#ifndef PATH_MANAGER_HPP
#define PATH_MANAGER_HPP

#include <iostream>

#define HOME 0

#if HOME == 1

const std::string LOCAL_PATH = "C:/Users/Stepan/Desktop/CG/5/";
const std::wstring LOCAL_PATH_W = L"C:/Users/Stepan/Desktop/CG/5/";

#else

const std::string LOCAL_PATH = "C:/Users/HUAWEI/Desktop/CG/5/";
const std::wstring LOCAL_PATH_W = L"C:/Users/HUAWEI/Desktop/CG/5/";

#endif // HOME

const std::string POST_PROCESS_FOLDER = "PostProcess/";
const std::wstring POST_PROCESS_FOLDER_W = L"PostProcess/";

const std::string TERRAIN_FOLDER = "Terrain/";
const std::wstring TERRAIN_FOLDER_W = L"Terrain/";

#endif // !PATH_MANAGER_HPP
