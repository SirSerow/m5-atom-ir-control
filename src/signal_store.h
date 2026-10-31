#pragma once

#include "ir_signal.h"

class StorageBackend {
 public:
  virtual ~StorageBackend() = default;
  virtual size_t read(uint8_t *bytes, size_t capacity) = 0;
  virtual size_t readPending(uint8_t *bytes, size_t capacity) = 0;
  // Write an inactive record; commit selects it only after verification.
  virtual bool write(const uint8_t *bytes, size_t length) = 0;
  virtual bool commit() = 0;
};

class SignalStore {
 public:
  explicit SignalStore(StorageBackend &backend) : backend_(backend) {}
  bool load(Signal &signal);
  bool save(const Signal &signal);

 private:
  StorageBackend &backend_;
  uint8_t bytes_[kMaxRecordBytes] = {};
};
