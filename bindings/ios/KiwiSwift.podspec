Pod::Spec.new do |spec|
  spec.name         = "KiwiSwift"
  spec.version      = "0.21.0"
  spec.summary      = "Korean Intelligent Word Identifier for iOS"
  spec.description  = <<-DESC
    KiwiSwift is the iOS binding for Kiwi, a Korean morphological analyzer.
    This framework provides native Swift API for Korean text processing,
    including tokenization, morphological analysis, and sentence splitting.
                       DESC

  spec.homepage     = "https://github.com/bab2min/Kiwi"
  spec.license      = { :type => "LGPL-3.0", :file => "../../LICENSE" }
  spec.author       = { "bab2min" => "bab2min@gmail.com" }

  spec.ios.deployment_target = "12.0"
  spec.osx.deployment_target = "10.15"

  spec.source       = { :git => "https://github.com/bab2min/Kiwi.git", :tag => "v#{spec.version}" }

  spec.source_files = [
    "swift/*.swift",
    "csrc/*.cpp",
    "include/*.h",
    "../../src/**/*.{cpp,h}",
    "../../include/kiwi/*.h",
    "../../third_party/streamvbyte/include/*.h",
    "../../third_party/streamvbyte/src/*.c"
  ]
  
  spec.public_header_files = "include/*.h"
  spec.private_header_files = "../../include/kiwi/*.h"

  spec.header_search_paths = [
    "../../include",
    "../../third_party/streamvbyte/include",
    "../../third_party/eigen",
    "../../third_party/cpp-btree",
    "../../third_party/json/include"
  ]

  spec.compiler_flags = [
    "-DIOS=1",
    "-DKIWI_IOS_BINDING=1",
    "-std=c++17",
    "-O3",
    "-fvisibility=hidden"
  ]

  spec.xcconfig = {
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++17",
    "CLANG_CXX_LIBRARY" => "libc++",
    "OTHER_CPLUSPLUSFLAGS" => "-DIOS=1 -DKIWI_IOS_BINDING=1"
  }

  spec.frameworks = "Foundation"
  spec.libraries = "c++"

  spec.requires_arc = true
  
  # Exclude files that are not needed for iOS
  spec.exclude_files = [
    "../../src/**/test*",
    "../../tools/**/*",
    "../../test/**/*"
  ]

  spec.prepare_command = <<-CMD
    # This would typically download or prepare model files
    echo "Preparing KiwiSwift for iOS..."
  CMD

end