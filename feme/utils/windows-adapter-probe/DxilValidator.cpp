//===- DxilValidator.cpp - Public SDK DXIL validation diagnostic ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <ole2.h>
#include <windows.h>

#include <dxcapi.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

static HRESULT report(const wchar_t *Operation, HRESULT Result) {
  std::wprintf(L"%ls: 0x%08lx\n", Operation,
               static_cast<unsigned long>(Result));
  return Result;
}

static HMODULE loadModule(const wchar_t *Directory, const wchar_t *Name) {
  std::wstring Path = std::wstring(Directory) + L"\\" + Name;
  wchar_t FullPath[32768];
  DWORD Length =
      GetFullPathNameW(Path.c_str(), ARRAYSIZE(FullPath), FullPath, nullptr);
  if (!Length || Length >= ARRAYSIZE(FullPath)) {
    report(L"GetFullPathNameW(validator module)",
           Length ? HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)
                  : HRESULT_FROM_WIN32(GetLastError()));
    return nullptr;
  }
  std::wprintf(L"Requested validator module: %ls\n", FullPath);
  HMODULE Module = LoadLibraryExW(FullPath, nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                      LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!Module) {
    report(L"LoadLibraryExW(validator module)",
           HRESULT_FROM_WIN32(GetLastError()));
    return nullptr;
  }
  Length = GetModuleFileNameW(Module, FullPath, ARRAYSIZE(FullPath));
  if (!Length || Length >= ARRAYSIZE(FullPath)) {
    report(L"GetModuleFileNameW(validator module)",
           Length ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)
                  : HRESULT_FROM_WIN32(GetLastError()));
    if (!FreeLibrary(Module))
      report(L"FreeLibrary(validator module)",
             HRESULT_FROM_WIN32(GetLastError()));
    return nullptr;
  }
  std::wprintf(L"Loaded validator module: %ls\n", FullPath);
  return Module;
}

static HRESULT validate(HMODULE Compiler, HMODULE ValidatorModule,
                        const wchar_t *InputPath, const wchar_t *OutputPath) {
  auto CompilerFactory = reinterpret_cast<DxcCreateInstanceProc>(
      GetProcAddress(Compiler, "DxcCreateInstance"));
  if (!CompilerFactory)
    return report(L"GetProcAddress(compiler DxcCreateInstance)",
                  HRESULT_FROM_WIN32(GetLastError()));
  auto ValidatorFactory = reinterpret_cast<DxcCreateInstanceProc>(
      GetProcAddress(ValidatorModule, "DxcCreateInstance"));
  if (!ValidatorFactory)
    return report(L"GetProcAddress(validator DxcCreateInstance)",
                  HRESULT_FROM_WIN32(GetLastError()));
  ComPtr<IDxcUtils> Utils;
  HRESULT Result =
      report(L"Create IDxcUtils",
             CompilerFactory(CLSID_DxcUtils, IID_PPV_ARGS(&Utils)));
  if (FAILED(Result))
    return Result;
  ComPtr<IDxcBlobEncoding> Input;
  Result =
      report(L"LoadFile(DXIL)", Utils->LoadFile(InputPath, nullptr, &Input));
  if (FAILED(Result))
    return Result;
  if (!Input || !Input->GetBufferSize() ||
      Input->GetBufferSize() > 16 * 1024 * 1024)
    return report(L"Invalid DXIL input size", E_FAIL);
  ComPtr<IDxcValidator> Validator;
  Result =
      report(L"Create IDxcValidator",
             ValidatorFactory(CLSID_DxcValidator, IID_PPV_ARGS(&Validator)));
  if (FAILED(Result))
    return Result;
  ComPtr<IDxcOperationResult> Operation;
  Result = report(L"IDxcValidator::Validate",
                  Validator->Validate(Input.Get(),
                                      OutputPath ? DxcValidatorFlags_InPlaceEdit
                                                 : DxcValidatorFlags_Default,
                                      &Operation));
  if (FAILED(Result))
    return Result;
  HRESULT Status = E_FAIL;
  Result = report(L"DXIL validation GetStatus", Operation->GetStatus(&Status));
  if (FAILED(Result))
    return Result;
  ComPtr<IDxcBlobEncoding> Errors;
  Result = report(L"DXIL validation GetErrorBuffer",
                  Operation->GetErrorBuffer(&Errors));
  if (FAILED(Result))
    return Result;
  if (Errors && Errors->GetBufferSize()) {
    auto *Data = static_cast<const char *>(Errors->GetBufferPointer());
    std::size_t Size = Errors->GetBufferSize();
    if (Data[Size - 1] == '\0')
      --Size;
    if (std::fwrite(Data, 1, Size, stderr) != Size)
      return report(L"Write validator diagnostics", E_FAIL);
  }
  report(L"DXIL validation status", Status);
  if (Status != S_OK)
    return FAILED(Status) ? Status : E_FAIL;
  if (!OutputPath)
    return S_OK;
  ComPtr<IDxcBlob> Output;
  Result = report(L"DXIL validation GetResult", Operation->GetResult(&Output));
  if (FAILED(Result))
    return Result;
  if (!Output || !Output->GetBufferSize() ||
      Output->GetBufferSize() > 16 * 1024 * 1024)
    return report(L"Invalid validated DXIL output", E_FAIL);
  HANDLE File = CreateFileW(OutputPath, GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (File == INVALID_HANDLE_VALUE)
    return report(L"CreateFileW(validated DXIL)",
                  HRESULT_FROM_WIN32(GetLastError()));
  DWORD Written = 0;
  if (!WriteFile(File, Output->GetBufferPointer(),
                 static_cast<DWORD>(Output->GetBufferSize()), &Written,
                 nullptr))
    Result = report(L"WriteFile(validated DXIL)",
                    HRESULT_FROM_WIN32(GetLastError()));
  else if (Written != Output->GetBufferSize())
    Result = report(L"Incomplete validated DXIL write", E_FAIL);
  if (!CloseHandle(File)) {
    HRESULT CloseResult = report(L"CloseHandle(validated DXIL)",
                                 HRESULT_FROM_WIN32(GetLastError()));
    if (SUCCEEDED(Result))
      Result = CloseResult;
  }
  if (SUCCEEDED(Result))
    std::wprintf(L"Validated DXIL written: %ls; bytes=%zu\n", OutputPath,
                 Output->GetBufferSize());
  return Result;
}

int wmain(int Argc, wchar_t **Argv) {
  if (Argc != 3 && Argc != 4) {
    std::fwprintf(stderr, L"Usage: %ls SDK-DLL-DIRECTORY INPUT [OUTPUT]\n",
                  Argv[0]);
    return 1;
  }
  HMODULE Compiler = loadModule(Argv[1], L"dxcompiler.dll");
  if (!Compiler)
    return 1;
  HMODULE Validator = loadModule(Argv[1], L"dxil.dll");
  HRESULT Result = Validator ? validate(Compiler, Validator, Argv[2],
                                        Argc == 4 ? Argv[3] : nullptr)
                             : E_FAIL;
  if (Validator && !FreeLibrary(Validator))
    Result =
        report(L"FreeLibrary(validator)", HRESULT_FROM_WIN32(GetLastError()));
  if (!FreeLibrary(Compiler))
    Result =
        report(L"FreeLibrary(compiler)", HRESULT_FROM_WIN32(GetLastError()));
  return FAILED(Result) ? 1 : 0;
}
