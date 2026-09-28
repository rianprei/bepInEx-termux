namespace {
unsigned int calls = 0;
}

extern "C" {
void *mono_get_root_domain() {
    ++calls;
    return nullptr;
}
void *mono_thread_attach(void *) {
    ++calls;
    return nullptr;
}
void *mono_domain_assembly_open(void *, const char *) {
    ++calls;
    return nullptr;
}
void *mono_assembly_get_image(void *) {
    ++calls;
    return nullptr;
}
void *mono_class_from_name(void *, const char *, const char *) {
    ++calls;
    return nullptr;
}
void *mono_class_get_method_from_name(void *, const char *, int) {
    ++calls;
    return nullptr;
}
unsigned int mono_test_call_count() {
    return calls;
}
}
