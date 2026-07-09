#pragma once
//
// Caricatore di moduli plugin da libreria dinamica (LoadLibrary/dlopen).
//
#include "module.hpp"
#include <memory>
#include <string>
#include <vector>

namespace sdrjo {

class LoadedModule {
public:
    ~LoadedModule();
    LoadedModule(LoadedModule&&) noexcept;
    LoadedModule& operator=(LoadedModule&&) noexcept;
    LoadedModule(const LoadedModule&) = delete;

    IModule* module() const { return module_.get(); }
    const std::string& path() const { return path_; }

private:
    friend class ModuleLoader;
    LoadedModule() = default;

    void* handle_ = nullptr;
    std::unique_ptr<IModule> module_;
    std::string path_;
};

class ModuleLoader {
public:
    // Carica un singolo modulo; lancia std::runtime_error con il motivo
    // (file mancante, simboli assenti, ABI incompatibile).
    static LoadedModule load(const std::string& libraryPath);

    // Carica tutti i moduli trovati in una cartella (*.dll / *.so).
    static std::vector<LoadedModule> loadDirectory(const std::string& dir,
                                                   std::vector<std::string>* errors = nullptr);

    // Cartella "modules" accanto all'eseguibile (indipendente dalla
    // directory corrente di lavoro).
    static std::string defaultModulesDir();
};

} // namespace sdrjo
