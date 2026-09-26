#ifndef LTE_OS_h__
#define LTE_OS_h__

#include "String.h"
#include "Vector.h"

LT_API bool OS_ChangeDir(String const& dir);

LT_API void OS_ConfigureSignalHandlers();

LT_API bool OS_CreateDir(String const& path);

LT_API void OS_CreatePath(String const& path);

LT_API bool OS_FileExists(String const& path);

LT_API String OS_GetAppDir();

LT_API String OS_GetDocumentsDir();

LT_API String OS_GetExecutableDir();

LT_API String OS_GetUserDataPath();

LT_API String OS_GetWorkingDir();

LT_API bool OS_IsDir(String const& path);

LT_API bool OS_IsFile(String const& path);

/* Whether a person is there to answer a dialog. Unattended, as the launcher's
   smoke mode runs in CI, message boxes and failed assertions print what they
   would have shown and the program exits with 1, rather than waiting for a
   click that never comes; Windows' own error dialogs are off too, and a crash
   prints its exception and stack before the program ends. */
LT_API bool OS_IsUnattended();
LT_API void OS_SetUnattended();

LT_API Vector<String> OS_ListDir(String const& path);

LT_API void OS_MessageBox(String const& title, String const& message);

LT_API bool OS_Spawn(String const& path);

LT_API void OS_WriteDump(String const& path);

#endif
