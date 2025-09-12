# StreamProvider API Documentation

## Overview

The StreamProvider interface allows KiwiBuilder to read model files from sources other than the traditional filesystem. This enables embedding models in applications, loading from databases, network sources, or any custom data source.

## C++ API

### Basic Usage

```cpp
#include <kiwi/Kiwi.h>

// Define a StreamProvider function
kiwi::KiwiBuilder::StreamProvider myProvider = [](const std::string& filename) -> std::unique_ptr<std::istream> {
    // Return stream for the requested file
    // Return nullptr if file not found
};

// Create KiwiBuilder with StreamProvider
kiwi::KiwiBuilder builder(myProvider, numThreads, options, modelType);
auto kiwi = builder.build();
```

### Memory-based Example

```cpp
#include <sstream>
#include <map>

std::map<std::string, std::string> modelFiles = {
    {"config.txt", "model_type=base\nversion=1.0"},
    {"basic.mdl", "binary_model_data"},
    {"combinedRule.txt", "rule1\nrule2"}
};

auto streamProvider = [&modelFiles](const std::string& filename) -> std::unique_ptr<std::istream> {
    auto it = modelFiles.find(filename);
    if (it != modelFiles.end()) {
        return std::make_unique<std::istringstream>(it->second);
    }
    return nullptr;
};

kiwi::KiwiBuilder builder(streamProvider, 4, kiwi::BuildOption::default_);
```

## C API

### Function Signature

```c
#include <kiwi/capi.h>

typedef size_t(*kiwi_stream_read_func)(void* user_data, char* buffer, size_t length);
typedef long long(*kiwi_stream_seek_func)(void* user_data, long long offset, int whence);
typedef void(*kiwi_stream_close_func)(void* user_data);

typedef struct {
    kiwi_stream_read_func  read;
    kiwi_stream_seek_func  seek;
    kiwi_stream_close_func close;
    void* user_data;
} kiwi_stream_object_t;

kiwi_builder_h kiwi_builder_init_stream(
    kiwi_stream_object_t (*stream_object_factory)(const char* filename), 
    int num_threads, 
    int options
);
```

### Usage Example

```c
// Stream implementation
typedef struct {
    const char* data;
    size_t size;
    size_t position;
} memory_stream_t;

size_t memory_read(void* user_data, char* buffer, size_t length) {
    memory_stream_t* stream = (memory_stream_t*)user_data;
    size_t available = stream->size - stream->position;
    size_t to_read = (length < available) ? length : available;
    
    memcpy(buffer, stream->data + stream->position, to_read);
    stream->position += to_read;
    return to_read;
}

long long memory_seek(void* user_data, long long offset, int whence) {
    memory_stream_t* stream = (memory_stream_t*)user_data;
    // Implement seeking logic
    return new_position;
}

void memory_close(void* user_data) {
    free(user_data);
}

kiwi_stream_object_t create_stream(const char* filename) {
    // Load file data (implementation specific)
    memory_stream_t* mem_stream = load_file_data(filename);
    
    kiwi_stream_object_t stream_obj = {0};
    stream_obj.read = memory_read;
    stream_obj.seek = memory_seek;
    stream_obj.close = memory_close;
    stream_obj.user_data = mem_stream;
    
    return stream_obj;
}

// Create KiwiBuilder
kiwi_builder_h builder = kiwi_builder_init_stream(
    create_stream,
    4,             // num_threads
    KIWI_BUILD_DEFAULT
);

if (builder) {
    kiwi_h kiwi = kiwi_builder_build(builder, NULL, 0);
    // Use kiwi...
    kiwi_close(kiwi);
    kiwi_builder_close(builder);
}
```

## Java API

### Interface Definition

```java
@FunctionalInterface
public interface StreamProvider {
    InputStream provide(String filename);
}
```

### Usage Example

```java
import kr.pe.bab2min.KiwiBuilder;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.util.Map;

// Create model file map
Map<String, byte[]> modelFiles = Map.of(
    "config.txt", "model_type=base\nversion=1.0".getBytes(),
    "basic.mdl", getBinaryModelData(),
    "combinedRule.txt", "rule1\nrule2".getBytes()
);

// StreamProvider implementation
KiwiBuilder.StreamProvider provider = filename -> {
    byte[] data = modelFiles.get(filename);
    return data != null ? new ByteArrayInputStream(data) : null;
};

// Create KiwiBuilder
KiwiBuilder builder = new KiwiBuilder(
    provider, 
    4,  // numWorkers
    KiwiBuilder.BuildOption.default_, 
    KiwiBuilder.ModelType.none
);

Kiwi kiwi = builder.build();
```

### Lambda Expression

```java
// Concise lambda syntax
KiwiBuilder builder = new KiwiBuilder(
    filename -> getModelStream(filename),
    4,
    KiwiBuilder.BuildOption.default_
);
```

## WASM/JavaScript API

### Function Definition

```javascript
// Define global StreamProvider function
function myStreamProviderFunction(filename) {
    // Return Uint8Array, ArrayBuffer, or null
    if (filename === 'config.txt') {
        return new TextEncoder().encode('model_type=base\nversion=1.0');
    }
    return null; // File not found
}

// Make globally available
window.myStreamProviderFunction = myStreamProviderFunction;
```

### Usage Example

```javascript
// Build request with StreamProvider
const buildRequest = {
    method: 'buildWithStreamProvider',
    args: [{
        streamProviderCallback: 'myStreamProviderFunction',
        modelType: 'none',
        integrateAllomorph: true,
        loadDefaultDict: false,
        userWords: [
            { word: '테스트', tag: 'NNG', score: 0.0 }
        ]
    }]
};

// Create KiwiBuilder
const kiwiId = kiwi.api(JSON.stringify(buildRequest));
const result = JSON.parse(kiwiId);
```

### Advanced Example with Fetch

```javascript
function networkStreamProvider(filename) {
    // Note: This is pseudo-code as WASM StreamProvider must be synchronous
    // In practice, you'd pre-fetch and cache the data
    const cachedData = modelCache[filename];
    if (cachedData) {
        return new Uint8Array(cachedData);
    }
    return null;
}

// Pre-cache model files
async function loadModels() {
    const files = ['config.txt', 'basic.mdl', 'combinedRule.txt'];
    for (const file of files) {
        const response = await fetch(`/models/${file}`);
        modelCache[file] = await response.arrayBuffer();
    }
}
```

## Use Cases

### 1. Embedded Applications

Package model files as binary resources in your application:

```cpp
// Embed model files as byte arrays
extern const char config_txt[];
extern const size_t config_txt_len;

auto provider = [](const std::string& filename) -> std::unique_ptr<std::istream> {
    if (filename == "config.txt") {
        return std::make_unique<std::istringstream>(
            std::string(config_txt, config_txt_len)
        );
    }
    return nullptr;
};
```

### 2. Network Loading

Load models from a CDN or server:

```java
KiwiBuilder.StreamProvider networkProvider = filename -> {
    try {
        URL url = new URL("https://cdn.example.com/models/" + filename);
        return url.openStream();
    } catch (IOException e) {
        return null;
    }
};
```

### 3. Database Storage

Store models in a database:

```cpp
auto dbProvider = [&database](const std::string& filename) -> std::unique_ptr<std::istream> {
    auto data = database.getModelFile(filename);
    if (!data.empty()) {
        return std::make_unique<std::istringstream>(data);
    }
    return nullptr;
};
```

### 4. Encrypted/Compressed Models

Decrypt or decompress models on-the-fly:

```c
int encrypted_provider(const char* filename, char* buffer, void* user_data) {
    if (buffer == NULL) {
        return get_encrypted_file_size(filename);
    }
    
    // Decrypt data into buffer
    return decrypt_file(filename, buffer);
}
```

## Error Handling

### C++
- Return `nullptr` from StreamProvider for missing files
- Exceptions thrown by StreamProvider are caught and handled

### C API
- Return `-1` from callback function to indicate error
- Check return value of `kiwi_builder_init_stream` for `NULL`

### Java
- Return `null` from `StreamProvider.provide()` for missing files
- IOException and other exceptions are handled gracefully

### WASM
- Return `null` or `undefined` from JavaScript function for missing files
- JavaScript exceptions are caught and handled

## Performance Considerations

1. **Caching**: Cache frequently accessed files in memory
2. **Lazy Loading**: Only load files when requested
3. **Compression**: Consider compressing model files and decompressing in StreamProvider
4. **Threading**: StreamProvider may be called from multiple threads in C++

## Limitations

1. **WordDetector**: When using StreamProvider, `extractWords()` and `extractAddWords()` methods are not available
2. **Synchronous**: All StreamProvider implementations must be synchronous
3. **WASM**: JavaScript callback function must be globally accessible
4. **Memory**: Large models loaded into memory may increase RAM usage

## Migration Guide

### From Filesystem to StreamProvider

**Before:**
```cpp
KiwiBuilder builder("/path/to/models", numThreads, options);
```

**After:**
```cpp
auto provider = [](const std::string& filename) {
    return std::make_unique<std::ifstream>("/path/to/models/" + filename, std::ios::binary);
};
KiwiBuilder builder(provider, numThreads, options);
```

This allows gradual migration and testing of the StreamProvider interface while maintaining the same functionality.