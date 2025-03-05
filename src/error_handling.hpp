#pragma once

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <memory>
#include <string>

/**
 * @brief Collects and manages error messages in a JSON format.
 */
class ErrorCollector {
public:
  /**
   * @brief Construct a new ErrorCollector object.
   */
  ErrorCollector() : doc_(nullptr) {}

  /**
   * @brief Add an error message for a given field.
   *
   * @param field The field associated with the error.
   * @param message The error message.
   */
  void add_error(const std::string &field, const std::string &message) {
    lazy_init();

    rapidjson::Value key;
    key.SetString(field.c_str(), static_cast<rapidjson::SizeType>(field.size()),
                  doc_->GetAllocator());

    rapidjson::Value msg;
    msg.SetString(message.c_str(),
                  static_cast<rapidjson::SizeType>(message.size()),
                  doc_->GetAllocator());

    if (doc_->HasMember(key)) {
      rapidjson::Value &existing = (*doc_)[key];
      if (!existing.IsArray()) {
        rapidjson::Value arr(rapidjson::kArrayType);
        arr.PushBack(existing, doc_->GetAllocator());
        existing = arr;
      }
      existing.PushBack(msg, doc_->GetAllocator());
    } else {
      doc_->AddMember(key, msg, doc_->GetAllocator());
    }
  }

  /**
   * @brief Add suberrors for a given field.
   *
   * The suberrors should be provided as a JSON string representing an object.
   * Each suberror key will be prefixed with the provided field and a dot.
   *
   * @param field The field associated with the suberrors.
   * @param json_errors A JSON string representing the suberrors.
   */
  void add_suberror(const std::string &field, const std::string &json_errors) {
    lazy_init();

    rapidjson::Document subdoc;
    if (subdoc.Parse(json_errors.c_str()).HasParseError() ||
        !subdoc.IsObject()) {
      add_error(field, "Invalid suberror JSON");
      return;
    }

    for (auto itr = subdoc.MemberBegin(); itr != subdoc.MemberEnd(); ++itr) {
      std::string combined_key =
          field + "." +
          std::string(itr->name.GetString(), itr->name.GetStringLength());

      rapidjson::Value key;
      key.SetString(combined_key.c_str(),
                    static_cast<rapidjson::SizeType>(combined_key.size()),
                    doc_->GetAllocator());

      rapidjson::Value value;
      value.CopyFrom(itr->value, doc_->GetAllocator());

      if (doc_->HasMember(key)) {
        rapidjson::Value &existing = (*doc_)[key];
        if (!existing.IsArray()) {
          rapidjson::Value arr(rapidjson::kArrayType);
          arr.PushBack(existing, doc_->GetAllocator());
          existing = arr;
        }
        existing.PushBack(value, doc_->GetAllocator());
      } else {
        doc_->AddMember(key, value, doc_->GetAllocator());
      }
    }
  }

  /**
   * @brief Check whether any errors have been recorded.
   *
   * @return true if errors exist, false otherwise.
   */
  bool has_errors() const { return doc_ && !doc_->ObjectEmpty(); }

  /**
   * @brief Convert the recorded errors to a JSON string.
   *
   * @return A JSON string representing the errors.
   */
  std::string to_json() const {
    if (!doc_) {
      return "";
    }
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    writer.SetIndent(' ', 2);
    doc_->Accept(writer);
    return buffer.GetString();
  }

private:
  /**
   * @brief Lazily initialize the JSON document.
   */
  void lazy_init() {
    if (!doc_) {
      doc_ = std::make_unique<rapidjson::Document>();
      doc_->SetObject();
    }
  }

  std::unique_ptr<rapidjson::Document> doc_;
};
