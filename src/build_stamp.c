/* build_stamp.c — touched before every build (see CMakeLists.txt), so
   __DATE__ and __TIME__ give the time of the build, not of this file. */
const char kBuildStamp[] = __DATE__ " " __TIME__;
