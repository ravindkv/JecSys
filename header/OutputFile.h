#pragma once
// ROOT output file with an atomic commit: everything is written to "<path>.tmp" and
// renamed to "<path>" only by commit(). A run that fails half-way therefore never
// replaces or truncates an existing result.
#include <memory>
#include <string>

#include "TFile.h"
#include "TObject.h"

class OutputFile {
 public:
  explicit OutputFile(const std::string& finalPath);
  ~OutputFile();  // removes the temporary file unless commit() was called
  OutputFile(const OutputFile&) = delete;
  OutputFile& operator=(const OutputFile&) = delete;

  // Write `object` at "dir/sub/name"; the directories are created on demand.
  void write(const std::string& objectPath, const TObject& object);
  // Store a text (card, configuration, ...) as a TNamed.
  void writeText(const std::string& objectPath, const std::string& text);

  void commit();
  const std::string& path() const { return finalPath_; }

 private:
  std::string finalPath_;
  std::string tmpPath_;
  std::unique_ptr<TFile> file_;
  bool committed_ = false;
};
