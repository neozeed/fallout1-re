#include "game/config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plib/db/db.h"
#include "plib/gnw/memory.h"

#define CONFIG_FILE_MAX_LINE_LENGTH 256

// The initial number of sections (or key-value) pairs in the config.
#define CONFIG_INITIAL_CAPACITY 10

static bool config_parse_line(Config* config, char* string);
static bool config_split_line(char* string, char* key, char* value);
static bool config_add_section(Config* config, const char* sectionKey);
static bool config_strip_white_space(char* string);

// 0x426540
bool config_init(Config* config)
{
    if (config == NULL) {
        return false;
    }

    if (assoc_init(config, CONFIG_INITIAL_CAPACITY, sizeof(ConfigSection), NULL) != 0) {
        return false;
    }

    return true;
}

// 0x42656C
void config_exit(Config* config)
{
	int sectionIndex;

    if (config == NULL) {
        return;
    }

    for (sectionIndex = 0; sectionIndex < config->size; sectionIndex++) {
		int keyValueIndex;

        assoc_pair* sectionEntry = &(config->list[sectionIndex]);
        ConfigSection* section = (ConfigSection*)sectionEntry->data;

        for (keyValueIndex = 0; keyValueIndex < section->size; keyValueIndex++) {
            assoc_pair* keyValueEntry = &(section->list[keyValueIndex]);

            char** value = (char**)keyValueEntry->data;
            mem_free(*value);
            *value = NULL;
        }

        assoc_free(section);
    }

    assoc_free(config);
}

// Parses command line argments and adds them into the config.
//
// The expected format of [argv] elements are "[section]key=value", otherwise
// the element is silently ignored.
//
// NOTE: This function trims whitespace in key-value pair, but not in section.
// I don't know if this is intentional or it's bug.
//
// 0x4265D0
bool config_cmd_line_parse(Config* config, int argc, char** argv)
{
	int arg;

    if (config == NULL) {
        return false;
    }

    for (arg = 0; arg < argc; arg++) {
        char* pch;
        char* string = argv[arg];
		char* sectionKey;
        char key[260];
        char value[260];

        // Find opening bracket.
        pch = strchr(string, '[');
        if (pch == NULL) {
            continue;
        }

        sectionKey = pch + 1;

        // Find closing bracket.
        pch = strchr(sectionKey, ']');
        if (pch == NULL) {
            continue;
        }

        *pch = '\0';

        if (config_split_line(pch + 1, key, value)) {
            if (!config_set_string(config, sectionKey, key, value)) {
                *pch = ']';
                return false;
            }
        }

        *pch = ']';
    }

    return true;
}

// 0x4266E0
bool config_get_string(Config* config, const char* sectionKey, const char* key, char** valuePtr)
{
	int sectionIndex;
	int index;
	assoc_pair* sectionEntry;
    ConfigSection* section;
	assoc_pair* keyValueEntry;

    if (config == NULL || sectionKey == NULL || key == NULL || valuePtr == NULL) {
        return false;
    }

    sectionIndex = assoc_search(config, sectionKey);
    if (sectionIndex == -1) {
        return false;
    }

    sectionEntry = &(config->list[sectionIndex]);
    section = (ConfigSection*)sectionEntry->data;

    index = assoc_search(section, key);
    if (index == -1) {
        return false;
    }

    keyValueEntry = &(section->list[index]);
    *valuePtr = *(char**)keyValueEntry->data;

    return true;
}

// 0x426728
bool config_set_string(Config* config, const char* sectionKey, const char* key, const char* value)
{
	int sectionIndex;
	assoc_pair* sectionEntry;
	ConfigSection* section;
	int index;
	char* valueCopy;

    if (config == NULL || sectionKey == NULL || key == NULL || value == NULL) {
        return false;
    }

    sectionIndex = assoc_search(config, sectionKey);
    if (sectionIndex == -1) {
        // FIXME: Looks like a bug, this function never returns -1, which will
        // eventually lead to crash.
        if (config_add_section(config, sectionKey) == -1) {
            return false;
        }
        sectionIndex = assoc_search(config, sectionKey);
    }

    sectionEntry = &(config->list[sectionIndex]);
    section = (ConfigSection*)sectionEntry->data;

    index = assoc_search(section, key);
    if (index != -1) {
        assoc_pair* keyValueEntry = &(section->list[index]);

        char** existingValue = (char**)keyValueEntry->data;
        mem_free(*existingValue);
        *existingValue = NULL;

        assoc_delete(section, key);
    }

    valueCopy = mem_strdup(value);
    if (valueCopy == NULL) {
        return false;
    }

    if (assoc_insert(section, key, &valueCopy) == -1) {
        mem_free(valueCopy);
        return false;
    }

    return true;
}

// 0x4267DC
bool config_get_value(Config* config, const char* sectionKey, const char* key, int* valuePtr)
{
    char* stringValue;

    if (valuePtr == NULL) {
        return false;
    }

    if (!config_get_string(config, sectionKey, key, &stringValue)) {
        return false;
    }

    *valuePtr = atoi(stringValue);

    return true;
}

// 0x426810
bool config_get_values(Config* config, const char* sectionKey, const char* key, int* arr, int count)
{
    char temp[CONFIG_FILE_MAX_LINE_LENGTH];
    char* string;

    if (arr == NULL || count < 2) {
        return false;
    }

    if (!config_get_string(config, sectionKey, key, &string)) {
        return false;
    }

    string = strncpy(temp, string, CONFIG_FILE_MAX_LINE_LENGTH - 1);

    while (1) {
        char* pch = strchr(string, ',');
        if (pch == NULL) {
            break;
        }

        count--;
        if (count == 0) {
            break;
        }

        *pch = '\0';
        *arr++ = atoi(string);
        string = pch + 1;
    }

    if (count <= 1) {
        *arr = atoi(string);
        return true;
    }

    return false;
}

// 0x4268E0
bool config_set_value(Config* config, const char* sectionKey, const char* key, int value)
{
    char stringValue[20];
    itoa(value, stringValue, 10);

    return config_set_string(config, sectionKey, key, stringValue);
}

// Reads .INI file into config.
//
// 0x426A00
bool config_load(Config* config, const char* filePath, bool isDb)
{
char string[CONFIG_FILE_MAX_LINE_LENGTH];

    if (config == NULL || filePath == NULL) {
        return false;
    }

    if (isDb) {
        DB_FILE* stream = db_fopen(filePath, "rb");
        if (stream != NULL) {
            while (db_fgets(string, sizeof(string), stream) != NULL) {
                config_parse_line(config, string);
            }
            db_fclose(stream);
        }
    } else {
        FILE* stream = fopen(filePath, "rt");
        if (stream != NULL) {
            while (fgets(string, sizeof(string), stream) != NULL) {
                config_parse_line(config, string);
            }

            fclose(stream);
        }

        // FIXME: This function returns `true` even if the file was not actually
        // read. I'm pretty sure it's bug.
    }

    return true;
}

// Writes config into .INI file.
//
// 0x426AA4
bool config_save(Config* config, const char* filePath, bool isDb)
{
    if (config == NULL || filePath == NULL) {
        return false;
    }

    if (isDb) {
		int sectionIndex;
        DB_FILE* stream = db_fopen(filePath, "wt");
        if (stream == NULL) {
            return false;
        }

        for (sectionIndex = 0; sectionIndex < config->size; sectionIndex++) {
			ConfigSection* section;
			int index;

            assoc_pair* sectionEntry = &(config->list[sectionIndex]);
            db_fprintf(stream, "[%s]\n", sectionEntry->name);

            section = (ConfigSection*)sectionEntry->data;
            for (index = 0; index < section->size; index++) {
                assoc_pair* keyValueEntry = &(section->list[index]);
                db_fprintf(stream, "%s=%s\n", keyValueEntry->name, *(char**)keyValueEntry->data);
            }

            db_fprintf(stream, "\n");
        }

        db_fclose(stream);
    } else {
		int sectionIndex;
        FILE* stream = fopen(filePath, "wt");
        if (stream == NULL) {
            return false;
        }

        for (sectionIndex = 0; sectionIndex < config->size; sectionIndex++) {
			int index;
			ConfigSection* section;

            assoc_pair* sectionEntry = &(config->list[sectionIndex]);
            fprintf(stream, "[%s]\n", sectionEntry->name);

            section = (ConfigSection*)sectionEntry->data;
            for (index = 0; index < section->size; index++) {
                assoc_pair* keyValueEntry = &(section->list[index]);
                fprintf(stream, "%s=%s\n", keyValueEntry->name, *(char**)keyValueEntry->data);
            }

            fprintf(stream, "\n");
        }

        fclose(stream);
    }

    return true;
}

// Parses a line from .INI file into config.
//
// A line either contains a "[section]" section key or "key=value" pair. In the
// first case section key is not added to config immediately, instead it is
// stored in |section| for later usage. This prevents empty
// sections in the config.
//
// In case of key-value pair it pretty straight forward - it adds key-value
// pair into previously read section key stored in |section|.
//
// Returns `true` when a section was parsed or key-value pair was parsed and
// added to the config, or `false` otherwise.
//
// 0x426C3C
static bool config_parse_line(Config* config, char* string)
{
    // 0x504C28
    static char section[CONFIG_FILE_MAX_LINE_LENGTH] = "unknown";
    char key[260];
    char value[260];

    char* pch;

    // Find comment marker and truncate the string.
    pch = strchr(string, ';');
    if (pch != NULL) {
        *pch = '\0';
    }

    // Find opening bracket.
    pch = strchr(string, '[');
    if (pch != NULL) {
        char* sectionKey = pch + 1;

        // Find closing bracket.
        pch = strchr(sectionKey, ']');
        if (pch != NULL) {
            *pch = '\0';
            strcpy(section, sectionKey);
            return config_strip_white_space(section);
        }
    }

    if (!config_split_line(string, key, value)) {
        return false;
    }

    return config_set_string(config, section, key, value);
}

// Splits "key=value" pair from [string] and copy appropriate parts into [key]
// and [value] respectively.
//
// Both key and value are trimmed.
//
// 0x426D14
static bool config_split_line(char* string, char* key, char* value)
{
	char* pch;

    if (string == NULL || key == NULL || value == NULL) {
        return false;
    }

    // Find equals character.
    pch = strchr(string, '=');
    if (pch == NULL) {
        return false;
    }

    *pch = '\0';

    strcpy(key, string);
    strcpy(value, pch + 1);

    *pch = '=';

    config_strip_white_space(key);
    config_strip_white_space(value);

    return true;
}

// Ensures the config has a section with specified key.
//
// Return `true` if section exists or it was successfully added, or `false`
// otherwise.
//
// 0x426DB8
static bool config_add_section(Config* config, const char* sectionKey)
{
    ConfigSection section;

    if (config == NULL || sectionKey == NULL) {
        return false;
    }

    if (assoc_search(config, sectionKey) != -1) {
        // Section already exists, no need to do anything.
        return true;
    }

    if (assoc_init(&section, CONFIG_INITIAL_CAPACITY, sizeof(char**), NULL) == -1) {
        return false;
    }

    if (assoc_insert(config, sectionKey, &section) == -1) {
        return false;
    }

    return true;
}

// Removes leading and trailing whitespace from the specified string.
//
// 0x426E18
static bool config_strip_white_space(char* string)
{
	int length;
	char* pch;

    if (string == NULL) {
        return false;
    }

    length = strlen(string);
    if (length == 0) {
        return true;
    }

    // Starting from the end of the string, loop while it's a whitespace and
    // decrement string length.
    pch = string + length - 1;
    while (length != 0 && isspace(*pch)) {
        length--;
        pch--;
    }

    // pch now points to the last non-whitespace character.
    pch[1] = '\0';

    // Starting from the beginning of the string loop while it's a whitespace
    // and decrement string length.
    pch = string;
    while (isspace(*pch)) {
        pch++;
        length--;
    }

    // pch now points for to the first non-whitespace character.
    memmove(string, pch, length + 1);

    return true;
}

// 0x426E98
bool config_get_double(Config* config, const char* sectionKey, const char* key, double* valuePtr)
{
    char* stringValue;

    if (valuePtr == NULL) {
        return false;
    }

    if (!config_get_string(config, sectionKey, key, &stringValue)) {
        return false;
    }

    *valuePtr = strtod(stringValue, NULL);

    return true;
}

// 0x426ECC
bool config_set_double(Config* config, const char* sectionKey, const char* key, double value)
{
    char stringValue[32];
    sprintf(stringValue, "%.6f", value);

    return config_set_string(config, sectionKey, key, stringValue);
}

// NOTE: Boolean-typed variant of [config_get_value].
bool configGetBool(Config* config, const char* sectionKey, const char* key, bool* valuePtr)
{
    int integerValue;

    if (valuePtr == NULL) {
        return false;
    }

    if (!config_get_value(config, sectionKey, key, &integerValue)) {
        return false;
    }

    *valuePtr = integerValue != 0;

    return true;
}

// NOTE: Boolean-typed variant of [configGetInt].
bool configSetBool(Config* config, const char* sectionKey, const char* key, bool value)
{
    return config_set_value(config, sectionKey, key, value ? 1 : 0);
}
