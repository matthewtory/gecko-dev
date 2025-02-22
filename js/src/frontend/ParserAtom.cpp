/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "frontend/ParserAtom.h"

#include <type_traits>

#include "jsnum.h"

#include "frontend/CompilationInfo.h"
#include "frontend/NameCollections.h"
#include "vm/JSContext.h"
#include "vm/Printer.h"
#include "vm/Runtime.h"
#include "vm/StringType.h"

using namespace js;
using namespace js::frontend;

namespace js {
namespace frontend {

static JS::OOM PARSER_ATOMS_OOM;

static JSAtom* GetWellKnownAtom(JSContext* cx, WellKnownAtomId kind) {
#define ASSERT_OFFSET_(idpart, id, text)       \
  static_assert(offsetof(JSAtomState, id) ==   \
                int32_t(WellKnownAtomId::id) * \
                    sizeof(js::ImmutablePropertyNamePtr));
  FOR_EACH_COMMON_PROPERTYNAME(ASSERT_OFFSET_);
#undef ASSERT_OFFSET_

#define ASSERT_OFFSET_(name, clasp)              \
  static_assert(offsetof(JSAtomState, name) ==   \
                int32_t(WellKnownAtomId::name) * \
                    sizeof(js::ImmutablePropertyNamePtr));
  JS_FOR_EACH_PROTOTYPE(ASSERT_OFFSET_);
#undef ASSERT_OFFSET_

  static_assert(int32_t(WellKnownAtomId::abort) == 0,
                "Unexpected order of WellKnownAtom");

  return (&cx->names().abort)[int32_t(kind)];
}

mozilla::GenericErrorResult<OOM&> RaiseParserAtomsOOMError(JSContext* cx) {
  js::ReportOutOfMemory(cx);
  return mozilla::Err(PARSER_ATOMS_OOM);
}

template <typename CharT>
/* static */ JS::Result<UniquePtr<ParserAtomEntry>, OOM&>
ParserAtomEntry::allocate(JSContext* cx,
                          mozilla::UniquePtr<CharT[], JS::FreePolicy>&& ptr,
                          uint32_t length, HashNumber hash) {
  MOZ_ASSERT(length > MaxInline<CharT>());

  ParserAtomEntry* entryPtr = cx->pod_malloc<ParserAtomEntry>();
  if (!entryPtr) {
    return RaiseParserAtomsOOMError(cx);
  }
  return UniquePtr<ParserAtomEntry>(
      new (entryPtr) ParserAtomEntry(std::move(ptr), length, hash));
}

template <typename CharT, typename SeqCharT>
/* static */ JS::Result<UniquePtr<ParserAtomEntry>, OOM&>
ParserAtomEntry::allocateInline(JSContext* cx,
                                InflatedChar16Sequence<SeqCharT> seq,
                                uint32_t length, HashNumber hash) {
  MOZ_ASSERT(length <= MaxInline<CharT>());

  ParserAtomEntry* uninitEntry =
      cx->pod_malloc_with_extra<ParserAtomEntry, CharT>(length);
  if (!uninitEntry) {
    return RaiseParserAtomsOOMError(cx);
  }

  CharT* entryBuf = ParserAtomEntry::inlineBufferPtr<CharT>(
      reinterpret_cast<ParserAtomEntry*>(uninitEntry));
  UniquePtr<ParserAtomEntry> entry(new (uninitEntry)
                                       ParserAtomEntry(entryBuf, length, hash));
  drainChar16Seq(entryBuf, seq, length);
  return entry;
}

bool ParserAtomEntry::equalsJSAtom(JSAtom* other) const {
  // Compare hashes and lengths first.
  if (hash_ != other->hash() || length_ != other->length()) {
    return false;
  }

  JS::AutoCheckCannotGC nogc;

  if (hasTwoByteChars()) {
    // Compare heap-allocated 16-bit chars to atom.
    return other->hasLatin1Chars()
               ? EqualChars(twoByteChars(), other->latin1Chars(nogc), length_)
               : EqualChars(twoByteChars(), other->twoByteChars(nogc), length_);
  }

  MOZ_ASSERT(hasLatin1Chars());
  return other->hasLatin1Chars()
             ? EqualChars(latin1Chars(), other->latin1Chars(nogc), length_)
             : EqualChars(latin1Chars(), other->twoByteChars(nogc), length_);
}

template <typename CharT>
UniqueChars ToPrintableStringImpl(JSContext* cx, mozilla::Range<CharT> str) {
  Sprinter sprinter(cx);
  if (!sprinter.init()) {
    return nullptr;
  }
  if (!QuoteString<QuoteTarget::String>(&sprinter, str)) {
    return nullptr;
  }
  return sprinter.release();
}

UniqueChars ParserAtomToPrintableString(JSContext* cx, const ParserAtom* atom) {
  size_t length = atom->length();

  return atom->hasLatin1Chars()
             ? ToPrintableStringImpl(
                   cx, mozilla::Range(atom->latin1Chars(), length))
             : ToPrintableStringImpl(
                   cx, mozilla::Range(atom->twoByteChars(), length));
}

bool ParserAtomEntry::isIndex(uint32_t* indexp) const {
  size_t len = length();
  if (len == 0 || len > UINT32_CHAR_BUFFER_LENGTH) {
    return false;
  }
  if (hasLatin1Chars()) {
    return mozilla::IsAsciiDigit(*latin1Chars()) &&
           js::CheckStringIsIndex(latin1Chars(), len, indexp);
  }
  return mozilla::IsAsciiDigit(*twoByteChars()) &&
         js::CheckStringIsIndex(twoByteChars(), len, indexp);
}

JS::Result<JSAtom*, OOM&> ParserAtomEntry::toJSAtom(
    JSContext* cx, CompilationInfo& compilationInfo) const {
  if (atomIndex_.is<AtomIndex>()) {
    return compilationInfo.input.atoms[atomIndex_.as<AtomIndex>()];
  }
  if (atomIndex_.is<WellKnownAtomId>()) {
    return GetWellKnownAtom(cx, atomIndex_.as<WellKnownAtomId>());
  }
  if (atomIndex_.is<StaticParserString1>()) {
    char16_t ch = static_cast<char16_t>(atomIndex_.as<StaticParserString1>());
    return cx->staticStrings().getUnit(ch);
  }
  if (atomIndex_.is<StaticParserString2>()) {
    size_t index = static_cast<size_t>(atomIndex_.as<StaticParserString2>());
    return cx->staticStrings().getLength2FromIndex(index);
  }

  JSAtom* atom;
  if (hasLatin1Chars()) {
    atom = AtomizeChars(cx, latin1Chars(), length());
  } else {
    atom = AtomizeChars(cx, twoByteChars(), length());
  }
  if (!atom) {
    return RaiseParserAtomsOOMError(cx);
  }
  auto index = compilationInfo.input.atoms.length();
  if (!compilationInfo.input.atoms.append(atom)) {
    return RaiseParserAtomsOOMError(cx);
  }
  atomIndex_ = mozilla::AsVariant(AtomIndex(index));
  return atom;
}

bool ParserAtomEntry::toNumber(JSContext* cx, double* result) const {
  return hasLatin1Chars() ? CharsToNumber(cx, latin1Chars(), length(), result)
                          : CharsToNumber(cx, twoByteChars(), length(), result);
}

#if defined(DEBUG) || defined(JS_JITSPEW)
void ParserAtomEntry::dump() const {
  js::Fprinter out(stderr);
  out.put("\"");
  dumpCharsNoQuote(out);
  out.put("\"\n");
}

void ParserAtomEntry::dumpCharsNoQuote(js::GenericPrinter& out) const {
  if (hasLatin1Chars()) {
    JSString::dumpCharsNoQuote<Latin1Char>(latin1Chars(), length(), out);
  } else {
    JSString::dumpCharsNoQuote<char16_t>(twoByteChars(), length(), out);
  }
}
#endif

ParserAtomsTable::ParserAtomsTable(JSRuntime* rt)
    : wellKnownTable_(*rt->commonParserNames) {}

template <typename CharT>
ParserAtomsTable::AddPtr ParserAtomsTable::lookupForAdd(
    JSContext* cx, InflatedChar16Sequence<CharT> seq) {
#ifdef DEBUG
  {
    // Sample more chars than longest tiny atom.
    constexpr int SAMPLE_LEN = 3;
    char16_t buf[SAMPLE_LEN];
    uint32_t len = 0;

    InflatedChar16Sequence<CharT> seqCopy = seq;

    for (int i = 0; i < SAMPLE_LEN; ++i) {
      if (!seqCopy.hasMore()) {
        break;
      }
      buf[len++] = seqCopy.next();
    }

    MOZ_ASSERT(wellKnownTable_.lookupTiny(buf, len) == nullptr,
               "Should have already checked for common tiny atoms");
  }
#endif

  // Check against well-known.
  SpecificParserAtomLookup<CharT> lookup(seq);
  const ParserAtom* wk = wellKnownTable_.lookupChar16Seq(lookup);
  if (wk) {
    return AddPtr(wk);
  }

  // Check for existing atom.
  return AddPtr(entrySet_.lookupForAdd(lookup), lookup.hash());
}

JS::Result<const ParserAtom*, OOM&> ParserAtomsTable::addEntry(
    JSContext* cx, AddPtr& addPtr, UniquePtr<ParserAtomEntry> entry) {
  ParserAtomEntry* entryPtr = entry.get();
  MOZ_ASSERT(!addPtr);
  if (!entrySet_.add(addPtr.inner().entrySetAddPtr, std::move(entry))) {
    return RaiseParserAtomsOOMError(cx);
  }
  return entryPtr->asAtom();
}

JS::Result<const ParserAtom*, OOM&> ParserAtomsTable::internLatin1Seq(
    JSContext* cx, AddPtr& addPtr, const Latin1Char* latin1Ptr,
    uint32_t length) {
  MOZ_ASSERT(!addPtr);

  InflatedChar16Sequence<Latin1Char> seq(latin1Ptr, length);
  UniquePtr<ParserAtomEntry> entry;

  // Allocate an entry for inline strings.
  if (length <= ParserAtomEntry::MaxInline<Latin1Char>()) {
    MOZ_TRY_VAR(entry, ParserAtomEntry::allocateInline<Latin1Char>(
                           cx, seq, length, addPtr.inner().hash));
    return addEntry(cx, addPtr, std::move(entry));
  }

  UniqueLatin1Chars copy = js::DuplicateString(cx, latin1Ptr, length);
  if (!copy) {
    return RaiseParserAtomsOOMError(cx);
  }
  MOZ_TRY_VAR(entry, ParserAtomEntry::allocate(cx, std::move(copy), length,
                                               addPtr.inner().hash));
  return addEntry(cx, addPtr, std::move(entry));
}

template <typename AtomCharT, typename SeqCharT>
JS::Result<const ParserAtom*, OOM&> ParserAtomsTable::internChar16Seq(
    JSContext* cx, AddPtr& addPtr, InflatedChar16Sequence<SeqCharT> seq,
    uint32_t length) {
  MOZ_ASSERT(!addPtr);

  UniquePtr<ParserAtomEntry> entry;

  // Allocate a fat entry for inline strings.
  if (length <= ParserAtomEntry::MaxInline<AtomCharT>()) {
    MOZ_TRY_VAR(entry, ParserAtomEntry::allocateInline<AtomCharT>(
                           cx, seq, length, addPtr.inner().hash));
    return addEntry(cx, addPtr, std::move(entry));
  }

  // Or copy to out-of-line contents.
  using UniqueCharsT = mozilla::UniquePtr<AtomCharT[], JS::FreePolicy>;
  UniqueCharsT copy(cx->pod_malloc<AtomCharT>(length));
  if (!copy) {
    return RaiseParserAtomsOOMError(cx);
  }
  ParserAtomEntry::drainChar16Seq<AtomCharT, SeqCharT>(copy.get(), seq, length);
  MOZ_TRY_VAR(entry, ParserAtomEntry::allocate(cx, std::move(copy), length,
                                               addPtr.inner().hash));
  return addEntry(cx, addPtr, std::move(entry));
}

static const uint16_t MAX_LATIN1_CHAR = 0xff;

JS::Result<const ParserAtom*, OOM&> ParserAtomsTable::internAscii(
    JSContext* cx, const char* asciiPtr, uint32_t length) {
  // ASCII strings are strict subsets of Latin1 strings.
  const Latin1Char* latin1Ptr = reinterpret_cast<const Latin1Char*>(asciiPtr);
  return internLatin1(cx, latin1Ptr, length);
}

JS::Result<const ParserAtom*, OOM&> ParserAtomsTable::internLatin1(
    JSContext* cx, const Latin1Char* latin1Ptr, uint32_t length) {
  // Check for tiny strings which are abundant in minified code.
  if (const ParserAtom* tiny = wellKnownTable_.lookupTiny(latin1Ptr, length)) {
    return tiny;
  }

  // Check for well-known or existing.
  InflatedChar16Sequence<Latin1Char> seq(latin1Ptr, length);
  AddPtr addPtr = lookupForAdd(cx, seq);
  if (addPtr) {
    return addPtr.get()->asAtom();
  }

  return internLatin1Seq(cx, addPtr, latin1Ptr, length);
}

JS::Result<const ParserAtom*, OOM&> ParserAtomsTable::internUtf8(
    JSContext* cx, const mozilla::Utf8Unit* utf8Ptr, uint32_t nbyte) {
  // Check for tiny strings which are abundant in minified code.
  // NOTE: The tiny atoms are all ASCII-only so we can directly look at the
  //        UTF-8 data without worrying about surrogates.
  if (const ParserAtom* tiny = wellKnownTable_.lookupTiny(
          reinterpret_cast<const Latin1Char*>(utf8Ptr), nbyte)) {
    return tiny;
  }

  // If source text is ASCII, then the length of the target char buffer
  // is the same as the length of the UTF8 input.  Convert it to a Latin1
  // encoded string on the heap.
  UTF8Chars utf8(utf8Ptr, nbyte);
  JS::SmallestEncoding minEncoding = FindSmallestEncoding(utf8);
  if (minEncoding == JS::SmallestEncoding::ASCII) {
    // As ascii strings are a subset of Latin1 strings, and each encoding
    // unit is the same size, we can reliably cast this `Utf8Unit*`
    // to a `Latin1Char*`.
    const Latin1Char* latin1Ptr = reinterpret_cast<const Latin1Char*>(utf8Ptr);
    return internLatin1(cx, latin1Ptr, nbyte);
  }

  // Check for existing.
  // NOTE: Well-known are all ASCII so have been handled above.
  InflatedChar16Sequence<mozilla::Utf8Unit> seq(utf8Ptr, nbyte);
  SpecificParserAtomLookup<mozilla::Utf8Unit> lookup(seq);
  MOZ_ASSERT(wellKnownTable_.lookupChar16Seq(lookup) == nullptr);
  AddPtr addPtr(entrySet_.lookupForAdd(lookup), lookup.hash());
  if (addPtr) {
    return addPtr.get()->asAtom();
  }

  // Compute length in code-points.
  uint32_t length = 0;
  InflatedChar16Sequence<mozilla::Utf8Unit> seqCopy = seq;
  while (seqCopy.hasMore()) {
    mozilla::Unused << seqCopy.next();
    length += 1;
  }

  // Otherwise, add new entry.
  bool wide = (minEncoding == JS::SmallestEncoding::UTF16);
  return wide ? internChar16Seq<char16_t>(cx, addPtr, seq, length)
              : internChar16Seq<Latin1Char>(cx, addPtr, seq, length);
}

JS::Result<const ParserAtom*, OOM&> ParserAtomsTable::internChar16(
    JSContext* cx, const char16_t* char16Ptr, uint32_t length) {
  // Check for tiny strings which are abundant in minified code.
  if (const ParserAtom* tiny = wellKnownTable_.lookupTiny(char16Ptr, length)) {
    return tiny;
  }

  InflatedChar16Sequence<char16_t> seq(char16Ptr, length);

  // Check for well-known or existing.
  AddPtr addPtr = lookupForAdd(cx, seq);
  if (addPtr) {
    return addPtr.get()->asAtom();
  }

  // Compute the target encoding.
  // NOTE: Length in code-points will be same, even if we deflate to Latin1.
  bool wide = false;
  InflatedChar16Sequence<char16_t> seqCopy = seq;
  while (seqCopy.hasMore()) {
    char16_t ch = seqCopy.next();
    if (ch > MAX_LATIN1_CHAR) {
      wide = true;
      break;
    }
  }

  // Otherwise, add new entry.
  return wide ? internChar16Seq<char16_t>(cx, addPtr, seq, length)
              : internChar16Seq<Latin1Char>(cx, addPtr, seq, length);
}

JS::Result<const ParserAtom*, OOM&> ParserAtomsTable::internJSAtom(
    JSContext* cx, CompilationInfo& compilationInfo, JSAtom* atom) {
  const ParserAtom* id;
  {
    JS::AutoCheckCannotGC nogc;

    auto result =
        atom->hasLatin1Chars()
            ? internLatin1(cx, atom->latin1Chars(nogc), atom->length())
            : internChar16(cx, atom->twoByteChars(nogc), atom->length());
    if (result.isErr()) {
      return result;
    }
    id = result.unwrap();
  }

  if (id->atomIndex_.is<mozilla::Nothing>()) {
    MOZ_ASSERT(id->equalsJSAtom(atom));

    auto index = AtomIndex(compilationInfo.input.atoms.length());
    if (!compilationInfo.input.atoms.append(atom)) {
      return RaiseParserAtomsOOMError(cx);
    }
    id->setAtomIndex(index);
  } else {
#ifdef DEBUG
    if (id->atomIndex_.is<AtomIndex>()) {
      MOZ_ASSERT(compilationInfo.input.atoms[id->atomIndex_.as<AtomIndex>()] ==
                 atom);
    } else if (id->atomIndex_.is<WellKnownAtomId>()) {
      MOZ_ASSERT(GetWellKnownAtom(cx, id->atomIndex_.as<WellKnownAtomId>()) ==
                 atom);
    }
#endif
  }
  return id;
}

static void FillChar16Buffer(char16_t* buf, const ParserAtomEntry* ent) {
  if (ent->hasLatin1Chars()) {
    std::copy(ent->latin1Chars(), ent->latin1Chars() + ent->length(), buf);
  } else {
    std::copy(ent->twoByteChars(), ent->twoByteChars() + ent->length(), buf);
  }
}

JS::Result<const ParserAtom*, OOM&> ParserAtomsTable::concatAtoms(
    JSContext* cx, mozilla::Range<const ParserAtom*> atoms) {
  bool latin1 = true;
  uint32_t catLen = 0;
  for (const ParserAtom* atom : atoms) {
    if (!atom->hasLatin1Chars()) {
      latin1 = false;
    }
    // Overflow check here, length
    if (atom->length() >= (ParserAtomEntry::MAX_LENGTH - catLen)) {
      return RaiseParserAtomsOOMError(cx);
    }
    catLen += atom->length();
  }

  if (latin1) {
    if (catLen <= ParserAtomEntry::MaxInline<Latin1Char>()) {
      Latin1Char buf[ParserAtomEntry::MaxInline<Latin1Char>()];
      size_t offset = 0;
      for (const ParserAtom* atom : atoms) {
        mozilla::PodCopy(buf + offset, atom->latin1Chars(), atom->length());
        offset += atom->length();
      }
      return internLatin1(cx, buf, catLen);
    }

    // Concatenate a latin1 string and add it to the table.
    UniqueLatin1Chars copy(cx->pod_malloc<Latin1Char>(catLen));
    if (!copy) {
      return RaiseParserAtomsOOMError(cx);
    }
    size_t offset = 0;
    for (const ParserAtom* atom : atoms) {
      mozilla::PodCopy(copy.get() + offset, atom->latin1Chars(),
                       atom->length());
      offset += atom->length();
    }

    InflatedChar16Sequence<Latin1Char> seq(copy.get(), catLen);

    // Check for well-known or existing.
    // NOTE: Tiny atoms are always Latin1/inline so we can ignore them here.
    AddPtr addPtr = lookupForAdd(cx, seq);
    if (addPtr) {
      return addPtr.get()->asAtom();
    }

    // Otherwise, add new entry.
    UniquePtr<ParserAtomEntry> entry;
    MOZ_TRY_VAR(entry, ParserAtomEntry::allocate(cx, std::move(copy), catLen,
                                                 addPtr.inner().hash));
    return addEntry(cx, addPtr, std::move(entry));
  }

  if (catLen <= ParserAtomEntry::MaxInline<char16_t>()) {
    char16_t buf[ParserAtomEntry::MaxInline<char16_t>()];
    size_t offset = 0;
    for (const ParserAtom* atom : atoms) {
      FillChar16Buffer(buf + offset, atom);
      offset += atom->length();
    }

    InflatedChar16Sequence<char16_t> seq(buf, catLen);

    // Check for well-known or existing.
    // NOTE: Tiny atoms are always Latin1/inline so we can ignore them here.
    AddPtr addPtr = lookupForAdd(cx, seq);
    if (addPtr) {
      return addPtr.get()->asAtom();
    }

    // Otherwise, add new entry.
    UniquePtr<ParserAtomEntry> entry;
    MOZ_TRY_VAR(entry, ParserAtomEntry::allocateInline<char16_t>(
                           cx, seq, catLen, addPtr.inner().hash));
    return addEntry(cx, addPtr, std::move(entry));
  }

  // Concatenate a char16 string and add it to the table.
  UniqueTwoByteChars copy(cx->pod_malloc<char16_t>(catLen));
  if (!copy) {
    return RaiseParserAtomsOOMError(cx);
  }
  size_t offset = 0;
  for (const ParserAtom* atom : atoms) {
    FillChar16Buffer(copy.get() + offset, atom);
    offset += atom->length();
  }

  InflatedChar16Sequence<char16_t> seq(copy.get(), catLen);

  // Check for well-known or existing.
  // NOTE: Tiny atoms are always Latin1/inline so we can ignore them here.
  AddPtr addPtr = lookupForAdd(cx, seq);
  if (addPtr) {
    return addPtr.get()->asAtom();
  }

  // Otherwise, add new entry.
  UniquePtr<ParserAtomEntry> entry;
  MOZ_TRY_VAR(entry, ParserAtomEntry::allocate(cx, std::move(copy), catLen,
                                               addPtr.inner().hash));
  return addEntry(cx, addPtr, std::move(entry));
}

template <typename CharT>
const ParserAtom* WellKnownParserAtoms::lookupChar16Seq(
    const SpecificParserAtomLookup<CharT>& lookup) const {
  EntrySet::Ptr get = entrySet_.readonlyThreadsafeLookup(lookup);
  if (get) {
    return get->get()->asAtom();
  }
  return nullptr;
}

bool WellKnownParserAtoms::initSingle(JSContext* cx, const ParserName** name,
                                      const char* str, WellKnownAtomId kind) {
  MOZ_ASSERT(name != nullptr);

  unsigned int len = strlen(str);

  MOZ_ASSERT(FindSmallestEncoding(UTF8Chars(str, len)) ==
             JS::SmallestEncoding::ASCII);

  // If we already reserved a tiny name, reuse the allocation but still point
  // the fixed `name` reference at it.
  if (const ParserAtom* tiny = lookupTiny(str, len)) {
    MOZ_ASSERT(len == 1 || len == 2);
    *name = tiny->asName();
    return true;
  }

  InflatedChar16Sequence<Latin1Char> seq(
      reinterpret_cast<const Latin1Char*>(str), len);
  SpecificParserAtomLookup<Latin1Char> lookup(seq);
  HashNumber hash = lookup.hash();

  UniquePtr<ParserAtomEntry> entry = nullptr;

  // Check for inline allocation.
  if (len <= ParserAtomEntry::MaxInline<Latin1Char>()) {
    auto maybeEntry =
        ParserAtomEntry::allocateInline<Latin1Char>(cx, seq, len, hash);
    if (maybeEntry.isErr()) {
      return false;
    }
    entry = maybeEntry.unwrap();

    // Do heap-allocation of contents.
  } else {
    UniqueLatin1Chars copy(cx->pod_malloc<Latin1Char>(len));
    if (!copy) {
      return false;
    }
    mozilla::PodCopy(copy.get(), reinterpret_cast<const Latin1Char*>(str), len);
    auto maybeEntry = ParserAtomEntry::allocate(cx, std::move(copy), len, hash);
    if (maybeEntry.isErr()) {
      return false;
    }
    entry = maybeEntry.unwrap();
  }
  entry->setWellKnownAtomId(kind);

  // Save name for returning after moving entry into set.
  const ParserName* nm = entry.get()->asName();
  if (!entrySet_.putNew(lookup, std::move(entry))) {
    js::ReportOutOfMemory(cx);
    return false;
  }

  *name = nm;
  return true;
}

bool WellKnownParserAtoms::initStaticStrings(JSContext* cx) {
  // Create known ParserAtoms for length-1 Latin1 strings.
  static_assert(WellKnownParserAtoms::ASCII_STATIC_LIMIT <=
                StaticStrings::UNIT_STATIC_LIMIT);
  constexpr size_t NUM_LENGTH1 = WellKnownParserAtoms::ASCII_STATIC_LIMIT;
  for (size_t i = 0; i < NUM_LENGTH1; ++i) {
    JS::AutoCheckCannotGC nogc;
    JSAtom* atom = cx->staticStrings().getUnit(char16_t(i));

    constexpr size_t len = 1;
    MOZ_ASSERT(atom->length() == len);

    InflatedChar16Sequence<Latin1Char> seq(atom->latin1Chars(nogc), len);
    SpecificParserAtomLookup<Latin1Char> lookup(seq);
    HashNumber hash = lookup.hash();

    auto maybeEntry =
        ParserAtomEntry::allocateInline<Latin1Char>(cx, seq, len, hash);
    if (maybeEntry.isErr()) {
      return false;
    }

    length1StaticTable_[i] = maybeEntry.unwrap();
    length1StaticTable_[i]->setStaticParserString1(StaticParserString1(i));
  }

  // Create known ParserAtoms for length-2 alpha-num strings.
  constexpr size_t NUM_LENGTH2 = NUM_SMALL_CHARS * NUM_SMALL_CHARS;
  for (size_t i = 0; i < NUM_LENGTH2; ++i) {
    JS::AutoCheckCannotGC nogc;
    JSAtom* atom = cx->staticStrings().getLength2FromIndex(i);

    constexpr size_t len = 2;
    MOZ_ASSERT(atom->length() == len);

    InflatedChar16Sequence<Latin1Char> seq(atom->latin1Chars(nogc), len);
    SpecificParserAtomLookup<Latin1Char> lookup(seq);
    HashNumber hash = lookup.hash();

    auto maybeEntry =
        ParserAtomEntry::allocateInline<Latin1Char>(cx, seq, len, hash);
    if (maybeEntry.isErr()) {
      return false;
    }

    length2StaticTable_[i] = maybeEntry.unwrap();
    length2StaticTable_[i]->setStaticParserString2(StaticParserString2(i));
  }

  return true;
}

bool WellKnownParserAtoms::init(JSContext* cx) {
  // Initialize the tiny strings before common names since there are some short
  // common names.
  if (!initStaticStrings(cx)) {
    return false;
  }

#define COMMON_NAME_INIT_(idpart, id, text)                \
  if (!initSingle(cx, &(id), text, WellKnownAtomId::id)) { \
    return false;                                          \
  }
  FOR_EACH_COMMON_PROPERTYNAME(COMMON_NAME_INIT_)
#undef COMMON_NAME_INIT_

#define COMMON_NAME_INIT_(name, clasp)                          \
  if (!initSingle(cx, &(name), #name, WellKnownAtomId::name)) { \
    return false;                                               \
  }
  JS_FOR_EACH_PROTOTYPE(COMMON_NAME_INIT_)
#undef COMMON_NAME_INIT_

  return true;
}

} /* namespace frontend */
} /* namespace js */

bool JSRuntime::initializeParserAtoms(JSContext* cx) {
  MOZ_ASSERT(!commonParserNames);

  if (parentRuntime) {
    commonParserNames = parentRuntime->commonParserNames;
    return true;
  }

  UniquePtr<js::frontend::WellKnownParserAtoms> names(
      js_new<js::frontend::WellKnownParserAtoms>());
  if (!names || !names->init(cx)) {
    return false;
  }

  commonParserNames = names.release();
  return true;
}

void JSRuntime::finishParserAtoms() {
  if (!parentRuntime) {
    js_delete(commonParserNames.ref());
  }
}
