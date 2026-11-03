// export.h — макрос экспорта символов gamecore (SHARED-библиотека).
#pragma once

#if defined(_WIN32)
  #if defined(GAMECORE_BUILD)
    #define GAMECORE_EXPORT __declspec(dllexport)
  #else
    #define GAMECORE_EXPORT __declspec(dllimport)
  #endif
#else
  #define GAMECORE_EXPORT __attribute__((visibility("default")))
#endif
