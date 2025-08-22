// CompatibleList.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// 對外：兩?表
extern std::unordered_map<std::string, std::vector<std::string>> bankingCompatibleTable;
extern std::unordered_map<std::string, std::vector<std::string>> debankingCompatibleTable;


// 小工具：從目標清單挑 4-bit/2-bit 型?（結合 .lib 可改進）
std::string pickMbffTarget(const std::vector<std::string>& candidates, int bit);
