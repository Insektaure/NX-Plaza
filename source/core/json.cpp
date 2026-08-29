#include "core/json.h"

#include "core/util.h"

namespace nxp::js {

std::string getStr(json_t* obj, const char* key, const std::string& def)
{
    json_t* v = json_object_get(obj, key);
    if (!json_is_string(v))
        return def;
    const char* s = json_string_value(v);
    return s ? std::string(s) : def;
}

int64_t getInt(json_t* obj, const char* key, int64_t def)
{
    json_t* v = json_object_get(obj, key);
    if (json_is_integer(v))
        return json_integer_value(v);
    if (json_is_real(v))
        return static_cast<int64_t>(json_real_value(v));
    return def;
}

double getReal(json_t* obj, const char* key, double def)
{
    json_t* v = json_object_get(obj, key);
    if (json_is_real(v))
        return json_real_value(v);
    if (json_is_integer(v))
        return static_cast<double>(json_integer_value(v));
    return def;
}

bool getBool(json_t* obj, const char* key, bool def)
{
    json_t* v = json_object_get(obj, key);
    if (json_is_boolean(v))
        return json_boolean_value(v);
    if (json_is_integer(v))
        return json_integer_value(v) != 0;
    return def;
}

json_t* getObj(json_t* obj, const char* key)
{
    json_t* v = json_object_get(obj, key);
    return json_is_object(v) ? v : nullptr;
}

json_t* getArr(json_t* obj, const char* key)
{
    json_t* v = json_object_get(obj, key);
    return json_is_array(v) ? v : nullptr;
}

std::vector<std::string> getStrArray(json_t* obj, const char* key, size_t maxItems)
{
    std::vector<std::string> out;
    json_t* arr = getArr(obj, key);
    if (!arr)
        return out;

    size_t index;
    json_t* value;
    json_array_foreach(arr, index, value) {
        if (out.size() >= maxItems)
            break;
        if (json_is_string(value))
            out.emplace_back(json_string_value(value));
    }
    return out;
}

json_t* strArray(const std::vector<std::string>& items)
{
    json_t* arr = json_array();
    for (const std::string& s : items)
        json_array_append_new(arr, json_string(s.c_str()));
    return arr;
}

json_t* readFile(const std::string& path)
{
    std::string text;
    if (!readWholeFile(path, text) || text.empty())
        return nullptr;
    return parse(text);
}

bool writeFile(const std::string& path, json_t* root)
{
    char* text = json_dumps(root, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
    if (!text)
        return false;
    bool ok = writeWholeFileAtomic(path, text);
    free(text);
    return ok;
}

json_t* parse(const std::string& text, std::string* errOut)
{
    json_error_t err;
    json_t* root = json_loadb(text.data(), text.size(), 0, &err);
    if (!root && errOut)
        *errOut = format("%s (line %d)", err.text, err.line);
    return root;
}

std::string dump(json_t* root)
{
    char* text = json_dumps(root, JSON_COMPACT);
    if (!text)
        return std::string();
    std::string out(text);
    free(text);
    return out;
}

} // namespace nxp::js
