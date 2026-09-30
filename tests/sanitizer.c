/* This sandbox cannot inspect /proc for LeakSanitizer. Tests allocate no heap. */
const char *__asan_default_options (void) { return "detect_leaks=0"; }
const char *__ubsan_default_options (void) { return "halt_on_error=1:print_stacktrace=1"; }
