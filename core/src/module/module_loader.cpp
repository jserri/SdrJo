#include "sdrjo/module/module_loader.hpp"

#include <filesystem>
#include <stdexcept>

#if defined(_WIN32)
  #include <windows.h>
  static void* osOpen(const char* p)       { return (void*)LoadLibraryA(p); }
  static void* osSym(void* h, const char* s){ return (void*)GetProcAddress((HMODULE)h, s); }
  static void  osClose(void* h)            { FreeLibrary((HMODULE)h); }
  static const char* kLibExt = ".dll";
#else
  #include <dlfcn.h>
  static void* osOpen(const char* p)        { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
  static void* osSym(void* h, const char* s){ return dlsym(h, s); }
  static void  osClose(void* h)             { dlclose(h); }
  static const char* kLibExt = ".so";
#endif

namespace sdrjo {

LoadedModule::~LoadedModule()
{
    module_.reset();          // distruggi il modulo prima di scaricare la lib
    if (handle_) osClose(handle_);
}

LoadedModule::LoadedModule(LoadedModule&& o) noexcept
    : handle_(o.handle_), module_(std::move(o.module_)), path_(std::move(o.path_))
{
    o.handle_ = nullptr;
}

LoadedModule& LoadedModule::operator=(LoadedModule&& o) noexcept
{
    if (this != &o) {
        module_.reset();
        if (handle_) osClose(handle_);
        handle_ = o.handle_;
        module_ = std::move(o.module_);
        path_ = std::move(o.path_);
        o.handle_ = nullptr;
    }
    return *this;
}

LoadedModule ModuleLoader::load(const std::string& libraryPath)
{
    void* handle = osOpen(libraryPath.c_str());
    if (!handle)
        throw std::runtime_error("impossibile aprire la libreria: " + libraryPath);

    auto abiFn = (SdrjoModuleAbiFn)osSym(handle, "sdrjo_module_abi");
    auto createFn = (SdrjoCreateModuleFn)osSym(handle, "sdrjo_create_module");
    if (!abiFn || !createFn) {
        osClose(handle);
        throw std::runtime_error("non e' un modulo SdrJo (simboli mancanti): " + libraryPath);
    }
    if (abiFn() != kModuleAbiVersion) {
        osClose(handle);
        throw std::runtime_error("versione ABI incompatibile: " + libraryPath);
    }

    LoadedModule lm;
    lm.handle_ = handle;
    lm.module_.reset(createFn());
    lm.path_ = libraryPath;
    if (!lm.module_) {
        throw std::runtime_error("la factory del modulo ha restituito null: " + libraryPath);
    }
    return lm;
}

std::vector<LoadedModule> ModuleLoader::loadDirectory(const std::string& dir,
                                                      std::vector<std::string>* errors)
{
    std::vector<LoadedModule> out;
    namespace fs = std::filesystem;
    if (!fs::exists(dir)) return out;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != kLibExt) continue;
        try {
            out.push_back(load(entry.path().string()));
        } catch (const std::exception& e) {
            if (errors) errors->push_back(e.what());
        }
    }
    return out;
}

} // namespace sdrjo
