#pragma once

#include <jansson.h>

#include <string>
#include <vector>

// Thin conveniences over jansson so the model code stays readable and never
// has to null-check a lookup. Every getter returns `def` when the key is
// missing or has the wrong type.
namespace nxp::js {

std::string getStr(json_t* obj, const char* key, const std::string& def = std::string());
int64_t getInt(json_t* obj, const char* key, int64_t def = 0);
double getReal(json_t* obj, const char* key, double def = 0.0);
bool getBool(json_t* obj, const char* key, bool def = false);
json_t* getObj(json_t* obj, const char* key);
json_t* getArr(json_t* obj, const char* key);
std::vector<std::string> getStrArray(json_t* obj, const char* key, size_t maxItems = 32);

// Builds a fresh json array. Ownership passes to the caller.
json_t* strArray(const std::vector<std::string>& items);

// Reads and parses a file. Returns nullptr on any failure. Caller decrefs.
json_t* readFile(const std::string& path);

// Serialises and writes atomically.
bool writeFile(const std::string& path, json_t* root);

// Parse a buffer. Returns nullptr on failure. Caller decrefs.
json_t* parse(const std::string& text, std::string* errOut = nullptr);

// Serialise to a compact string.
std::string dump(json_t* root);

} // namespace nxp::js
