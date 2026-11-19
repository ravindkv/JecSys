#pragma once
// Read-only ROOT file. Objects are returned as owned copies that are detached from
// the file, so they stay valid after the InputFile is gone.
#include <memory>
#include <string>

#include "JecSysError.h"
#include "TFile.h"
#include "TH1.h"

class InputFile {
 public:
  explicit InputFile(const std::string& path);

  const std::string& path() const { return path_; }
  bool has(const std::string& objectPath) const;

  // Owned copy of the object; throws JecSysError (file + object path) if it is missing
  // or has another type.
  template <class T>
  std::unique_ptr<T> get(const std::string& objectPath) const {
    std::unique_ptr<T> object = find<T>(objectPath);
    if (!object) {
      throw JecSysError("Object '" + objectPath + "' (" + T::Class_Name() + ") not found in " +
                        path_);
    }
    return object;
  }

  // Same, but returns an empty pointer if the object is missing.
  template <class T>
  std::unique_ptr<T> find(const std::string& objectPath) const {
    TObject* object = file_->Get(objectPath.c_str());
    if (!object || !object->InheritsFrom(T::Class())) return nullptr;
    std::unique_ptr<T> copy(static_cast<T*>(object->Clone()));
    detach(copy.get());
    return copy;
  }

  // Text stored as a TNamed title; throws if missing.
  std::string getText(const std::string& objectPath) const;

  TFile& file() const { return *file_; }

 private:
  static void detach(TObject* object) {
    if (auto* hist = dynamic_cast<TH1*>(object)) hist->SetDirectory(nullptr);
  }
  std::string path_;
  std::unique_ptr<TFile> file_;
};
