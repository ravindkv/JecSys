#include "InputFile.h"

#include "TNamed.h"

InputFile::InputFile(const std::string& path) : path_(path) {
  file_.reset(TFile::Open(path.c_str(), "READ"));
  if (!file_ || file_->IsZombie()) throw JecSysError("Cannot open input file '" + path + "'");
}

bool InputFile::has(const std::string& objectPath) const {
  return file_->Get(objectPath.c_str()) != nullptr;
}

std::string InputFile::getText(const std::string& objectPath) const {
  auto* named = dynamic_cast<TNamed*>(file_->Get(objectPath.c_str()));
  if (!named) throw JecSysError("Text '" + objectPath + "' not found in " + path_);
  return named->GetTitle();
}
