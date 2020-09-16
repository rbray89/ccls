// Copyright 2017-2018 ccls Authors
// SPDX-License-Identifier: Apache-2.0

#include "message_handler.hh"
#include "query.hh"

#include <string_view>

namespace ccls {
namespace {
namespace MarkupKind {
constexpr std::string_view PlainText = "plaintext";
constexpr std::string_view Markdown = "markdown";
} // namespace MarkupKind

struct MarkupContent {
  std::string_view kind{MarkupKind::Markdown};
  std::string value;
};
struct Hover {
  MarkupContent contents;
  std::optional<lsRange> range;

  void add(MarkupContent const &m) {
    contents.kind = m.kind;
    add_separator();
    contents.value += m.value;
  }

  void add(std::string_view hover) {
    contents.kind = MarkupKind::PlainText;
    add_separator();
    contents.value += hover;
  }

private:
  void add_separator() {
    if (!contents.value.empty()) {
      contents.value += "\n___\n";
      contents.kind = MarkupKind::Markdown;
    }
  }
};

REFLECT_STRUCT(MarkupContent, kind, value);
REFLECT_STRUCT(Hover, contents, range);

const char *languageIdentifier(LanguageId lang) {
  switch (lang) {
  // clang-format off
  case LanguageId::C: return "c";
  case LanguageId::Cpp: return "cpp";
  case LanguageId::ObjC: return "objective-c";
  case LanguageId::ObjCpp: return "objective-cpp";
  default: return "";
    // clang-format on
  }
}

std::string markdown_code(LanguageId lang, std::string_view code) {
  std::string str;
  str.reserve(3 + 13 + 1 + code.size() + 4 + 1);
  str = "```";
  str += languageIdentifier(lang);
  str += "\n";
  str += code;
  str += "\n```";
  return str;
}

// Returns the hover or detailed name for `sym`, if any.
std::pair<std::optional<MarkupContent>, std::optional<MarkupContent>>
getHover(DB *db, LanguageId lang, SymbolRef sym, int file_id) {
  const char *comments = nullptr;
  std::optional<MarkupContent> ls_comments, hover;
  withEntity(db, sym, [&](const auto &entity) {
    for (auto &d : entity.def) {
      if (!comments && d.comments[0])
        comments = d.comments;
      if (d.spell) {
        if (d.comments[0])
          comments = d.comments;
        if (const char *s =
                d.hover[0] ? d.hover
                           : d.detailed_name[0] ? d.detailed_name : nullptr) {
          if (!hover)
            hover = {MarkupKind::Markdown, markdown_code(lang, s)};
          else if (strlen(s) > hover->value.size())
            hover->value = s;
        }
        if (d.spell->file_id == file_id)
          break;
      }
    }
    if (!hover && entity.def.size()) {
      auto &d = entity.def[0];
      hover = MarkupContent{};
      if (d.hover[0])
        hover->value = markdown_code(lang, d.hover);
      else if (d.detailed_name[0])
        hover->value = markdown_code(lang, d.detailed_name);
    }
    if (comments)
      ls_comments = MarkupContent{MarkupKind::Markdown, comments};
  });
  return {hover, ls_comments};
}
} // namespace

void MessageHandler::textDocument_hover(TextDocumentPositionParam &param,
                                        ReplyOnce &reply) {
  auto [file, wf] = findOrFail(param.textDocument.uri.getPath(), reply);
  if (!wf)
    return;

  Hover result;
  for (SymbolRef sym : findSymbolsAtLocation(wf, file, param.position)) {
    std::optional<lsRange> ls_range =
        getLsRange(wfiles->getFile(file->def->path), sym.range);
    if (!ls_range)
      continue;

    auto [hover, comments] = getHover(db, file->def->language, sym, file->id);
    if (comments || hover) {
      result.range = *ls_range;
      if (comments)
        result.add(*comments);
      if (hover)
        result.add(*hover);
      if (sym.kind == Kind::Type) {
        unsigned size = db->getType(sym.usr).type_size;
        if (size != 0) {
          result.add("sizeof: " + std::to_string(size));
        }
      }
      break;
    }
  }

  reply(result);
}
} // namespace ccls
