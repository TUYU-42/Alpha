#include "place.h"
#include <iostream>

Placer::Placer(const std::unordered_map<std::string, LefMacroInfo>& macroMap,
    const std::vector<ComponentInfo>& components)
    : macroMap_(macroMap)
{
    // 建立 instanceName -> ComponentInfo 的快速查表
    for (const auto& comp : components) {
        instanceMap_[comp.name] = comp;  // comp.name 是 instanceName
    }
}

const LefMacroInfo* Placer::getMacroInfoByInstance(const std::string& instanceName) const {
    // 先找 instance
    auto it = instanceMap_.find(instanceName);
    if (it == instanceMap_.end()) {
        std::cerr << "[Placer] Instance not found: " << instanceName << std::endl;
        return nullptr;
    }
    // 再找 macro info
    const std::string& cellType = it->second.cellType;
    auto mit = macroMap_.find(cellType);
    if (mit == macroMap_.end()) {
        std::cerr << "[Placer] Macro (cellType) not found: " << cellType << std::endl;
        return nullptr;
    }
    return &(mit->second);
}

const ComponentInfo* Placer::getComponent(const std::string& instanceName) const {
    auto it = instanceMap_.find(instanceName);
    if (it == instanceMap_.end())
        return nullptr;
    return &(it->second);
}

std::vector<std::string> Placer::getAllInstanceNames() const {
    std::vector<std::string> names;
    for (const auto& kv : instanceMap_) {
        names.push_back(kv.first);
    }
    return names;
}

void Placer::printSomeMappings(int n) const {
    std::cout << "====== instance map macro test ======/n";
    int count = 0;
    for (const auto& kv : instanceMap_) {
        std::cout << kv.first << " (" << kv.second.cellType << ") => ";
        const LefMacroInfo* m = getMacroInfoByInstance(kv.first);
        if (m) std::cout << m->sizeX << " x " << m->sizeY;
        else std::cout << "[macro type not found!]";
        std::cout << std::endl;
        if (++count >= n) break;
    }
}
