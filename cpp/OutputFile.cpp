#include "OutputFile.h"

#include <filesystem>

#include "JecSysError.h"
#include "TNamed.h"
#include "TextTools.h"

OutputFile::OutputFile(const std::string& finalPath)
    : finalPath_(finalPath), tmpPath_(finalPath + ".tmp") {
  textTools::makeDirs(textTools::dirName(finalPath));
  file_.reset(TFile::Open(tmpPath_.c_str(), "RECREATE"));
  if (!file_ || file_->IsZombie()) throw JecSysError("Cannot create output file '" + tmpPath_ + "'");
}

OutputFile::~OutputFile() {
  if (committed_) return;
  if (file_) file_->Close();
  std::error_code ignored;
  std::filesystem::remove(tmpPath_, ignored);
}

void OutputFile::write(const std::string& objectPath, const TObject& object) {
  const std::size_t slash = objectPath.rfind('/');
  const std::string dirPath = slash == std::string::npos ? "" : objectPath.substr(0, slash);
  const std::string name = slash == std::string::npos ? objectPath : objectPath.substr(slash + 1);
  TDirectory* dir = file_.get();
  if (!dirPath.empty()) {
    dir = file_->GetDirectory(dirPath.c_str());
    if (!dir) dir = file_->mkdir(dirPath.c_str(), "", true);  // creates parents too
    if (!dir) throw JecSysError("Cannot create directory '" + dirPath + "' in " + tmpPath_);
  }
  if (dir->WriteTObject(&object, name.c_str(), "Overwrite") <= 0) {
    throw JecSysError("Cannot write '" + objectPath + "' to " + tmpPath_);
  }
}

void OutputFile::writeText(const std::string& objectPath, const std::string& text) {
  const std::size_t slash = objectPath.rfind('/');
  const std::string name = slash == std::string::npos ? objectPath : objectPath.substr(slash + 1);
  write(objectPath, TNamed(name.c_str(), text.c_str()));
}

void OutputFile::commit() {
  file_->Write();
  file_->Close();
  file_.reset();
  std::error_code error;
  std::filesystem::rename(tmpPath_, finalPath_, error);
  if (error) throw JecSysError("Cannot rename " + tmpPath_ + " to " + finalPath_ + ": " + error.message());
  committed_ = true;
}
