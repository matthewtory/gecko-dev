/* clang-format off */
/* -*- Mode: Objective-C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* clang-format on */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HyperTextAccessibleWrap.h"

#include "Accessible-inl.h"
#include "HTMLListAccessible.h"
#include "nsAccUtils.h"
#include "nsFrameSelection.h"
#include "TextRange.h"
#include "TreeWalker.h"

using namespace mozilla;
using namespace mozilla::a11y;

// HyperTextIterator

class HyperTextIterator {
 public:
  HyperTextIterator(HyperTextAccessible* aStartContainer, int32_t aStartOffset,
                    HyperTextAccessible* aEndContainer, int32_t aEndOffset,
                    bool aSkipBullet = false)
      : mCurrentContainer(aStartContainer),
        mCurrentStartOffset(aStartOffset),
        mCurrentEndOffset(aStartOffset),
        mEndContainer(aEndContainer),
        mEndOffset(aEndOffset),
        mSkipBullet(aSkipBullet) {}

  bool Next();

  int32_t SegmentLength();

  // If offset is set to a child hyperlink, adjust it so it set on the first
  // offset in the deepest link. Or, if the offset to the last character, set it
  // to the outermost end offset in an ancestor. Returns true if iterator was
  // mutated.
  bool NormalizeForward();

  // If offset is set right after child hyperlink, adjust it so it set on the
  // last offset in the deepest link. Or, if the offset is on the first
  // character of a link, set it to the outermost start offset in an ancestor.
  // Returns true if iterator was mutated.
  bool NormalizeBackward();

  HyperTextAccessible* mCurrentContainer;
  int32_t mCurrentStartOffset;
  int32_t mCurrentEndOffset;

 private:
  int32_t NextLinkOffset();

  HyperTextAccessible* mEndContainer;
  int32_t mEndOffset;

  bool mSkipBullet;
};

bool HyperTextIterator::NormalizeForward() {
  if (mCurrentStartOffset == nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT ||
      mCurrentStartOffset >=
          static_cast<int32_t>(mCurrentContainer->CharacterCount())) {
    // If this is the end of the current container, mutate to its parent's
    // end offset.
    if (!mCurrentContainer->IsLink()) {
      // If we are not a link, it is a root hypertext accessible.
      return false;
    }
    uint32_t endOffset = mCurrentContainer->EndOffset();
    if (endOffset != 0) {
      mCurrentContainer = mCurrentContainer->Parent()->AsHyperText();
      mCurrentStartOffset = endOffset;

      // Call NormalizeForward recursively to get top-most link if at the end of
      // one, or innermost link if at the beginning.
      NormalizeForward();
      return true;
    }
  } else {
    Accessible* link = mCurrentContainer->LinkAt(
        mCurrentContainer->LinkIndexAtOffset(mCurrentStartOffset));

    // If there is a link at this offset, mutate into it.
    if (link && link->IsHyperText()) {
      mCurrentContainer = link->AsHyperText();
      if (mSkipBullet && link->IsHTMLListItem()) {
        Accessible* bullet = link->AsHTMLListItem()->Bullet();
        mCurrentStartOffset = bullet ? nsAccUtils::TextLength(bullet) : 0;
      } else {
        mCurrentStartOffset = 0;
      }

      // Call NormalizeForward recursively to get top-most embedding ancestor
      // if at the end of one, or innermost link if at the beginning.
      NormalizeForward();
      return true;
    }
  }

  return false;
}

bool HyperTextIterator::NormalizeBackward() {
  if (mCurrentStartOffset == 0) {
    // If this is the start of the current container, mutate to its parent's
    // start offset.
    if (!mCurrentContainer->IsLink()) {
      // If we are not a link, it is a root hypertext accessible.
      return false;
    }

    uint32_t startOffset = mCurrentContainer->StartOffset();
    mCurrentContainer = mCurrentContainer->Parent()->AsHyperText();
    mCurrentStartOffset = startOffset;

    // Call NormalizeBackward recursively to get top-most link if at the
    // beginning of one, or innermost link if at the end.
    NormalizeBackward();
    return true;
  } else {
    Accessible* link =
        mCurrentContainer->GetChildAtOffset(mCurrentStartOffset - 1);

    // If there is a link before this offset, mutate into it,
    // and set the offset to its last character.
    if (link && link->IsHyperText()) {
      mCurrentContainer = link->AsHyperText();
      mCurrentStartOffset = mCurrentContainer->CharacterCount();

      // Call NormalizeBackward recursively to get top-most top-most embedding
      // ancestor if at the beginning of one, or innermost link if at the end.
      NormalizeBackward();
      return true;
    }

    if (mSkipBullet && mCurrentContainer->IsHTMLListItem() &&
        mCurrentContainer->AsHTMLListItem()->Bullet() == link) {
      mCurrentStartOffset = 0;
      NormalizeBackward();
      return true;
    }
  }

  return false;
}

int32_t HyperTextIterator::SegmentLength() {
  int32_t endOffset = mCurrentEndOffset < 0
                          ? mCurrentContainer->CharacterCount()
                          : mCurrentEndOffset;

  return endOffset - mCurrentStartOffset;
}

int32_t HyperTextIterator::NextLinkOffset() {
  int32_t linkCount = mCurrentContainer->LinkCount();
  for (int32_t i = 0; i < linkCount; i++) {
    Accessible* link = mCurrentContainer->LinkAt(i);
    MOZ_ASSERT(link);
    int32_t linkStartOffset = link->StartOffset();
    if (mCurrentStartOffset < linkStartOffset) {
      return linkStartOffset;
    }
  }

  return -1;
}

bool HyperTextIterator::Next() {
  if (mCurrentContainer == mEndContainer &&
      (mCurrentEndOffset == -1 || mEndOffset <= mCurrentEndOffset)) {
    return false;
  } else {
    mCurrentStartOffset = mCurrentEndOffset;
    NormalizeForward();
  }

  int32_t nextLinkOffset = NextLinkOffset();
  if (mCurrentContainer == mEndContainer &&
      (nextLinkOffset == -1 || nextLinkOffset > mEndOffset)) {
    mCurrentEndOffset =
        mEndOffset < 0 ? mEndContainer->CharacterCount() : mEndOffset;
  } else {
    mCurrentEndOffset = nextLinkOffset < 0 ? mCurrentContainer->CharacterCount()
                                           : nextLinkOffset;
  }

  return mCurrentStartOffset != mCurrentEndOffset;
}

void HyperTextAccessibleWrap::TextForRange(nsAString& aText,
                                           int32_t aStartOffset,
                                           HyperTextAccessible* aEndContainer,
                                           int32_t aEndOffset) {
  HyperTextIterator iter(this, aStartOffset, aEndContainer, aEndOffset);
  while (iter.Next()) {
    nsAutoString text;
    iter.mCurrentContainer->TextSubstring(iter.mCurrentStartOffset,
                                          iter.mCurrentEndOffset, text);
    aText.Append(text);
  }
}

nsIntRect HyperTextAccessibleWrap::BoundsForRange(
    int32_t aStartOffset, HyperTextAccessible* aEndContainer,
    int32_t aEndOffset) {
  nsIntRect rect;
  HyperTextIterator iter(this, aStartOffset, aEndContainer, aEndOffset);
  while (iter.Next()) {
    nsIntRect stringRect = iter.mCurrentContainer->TextBounds(
        iter.mCurrentStartOffset, iter.mCurrentEndOffset);
    rect.UnionRect(rect, stringRect);
  }

  return rect;
}

int32_t HyperTextAccessibleWrap::LengthForRange(
    int32_t aStartOffset, HyperTextAccessible* aEndContainer,
    int32_t aEndOffset) {
  int32_t length = 0;
  HyperTextIterator iter(this, aStartOffset, aEndContainer, aEndOffset, true);
  while (iter.Next()) {
    length += iter.SegmentLength();
  }

  return length;
}

void HyperTextAccessibleWrap::OffsetAtIndex(int32_t aIndex,
                                            HyperTextAccessible** aContainer,
                                            int32_t* aOffset) {
  int32_t index = aIndex;
  HyperTextIterator iter(this, 0, this, CharacterCount(), true);
  while (iter.Next()) {
    int32_t segmentLength = iter.SegmentLength();
    if (index <= segmentLength) {
      *aContainer = iter.mCurrentContainer;
      *aOffset = iter.mCurrentStartOffset + index;
      break;
    }
    index -= segmentLength;
  }
}

void HyperTextAccessibleWrap::LeftWordAt(int32_t aOffset,
                                         HyperTextAccessible** aStartContainer,
                                         int32_t* aStartOffset,
                                         HyperTextAccessible** aEndContainer,
                                         int32_t* aEndOffset) {
  TextPoint here(this, aOffset);
  TextPoint start =
      FindTextPoint(aOffset, eDirPrevious, eSelectWord, eStartWord);
  if (!start.mContainer) {
    return;
  }

  if ((NativeState() & states::EDITABLE) &&
      !(start.mContainer->NativeState() & states::EDITABLE)) {
    // The word search crossed an editable boundary. Return the first word of
    // the editable root.
    return EditableRoot()->RightWordAt(0, aStartContainer, aStartOffset,
                                       aEndContainer, aEndOffset);
  }

  TextPoint end =
      static_cast<HyperTextAccessibleWrap*>(start.mContainer)
          ->FindTextPoint(start.mOffset, eDirNext, eSelectWord, eEndWord);
  if (end < here) {
    *aStartContainer = end.mContainer;
    *aEndContainer = here.mContainer;
    *aStartOffset = end.mOffset;
    *aEndOffset = here.mOffset;
  } else {
    *aStartContainer = start.mContainer;
    *aEndContainer = end.mContainer;
    *aStartOffset = start.mOffset;
    *aEndOffset = end.mOffset;
  }
}

void HyperTextAccessibleWrap::RightWordAt(int32_t aOffset,
                                          HyperTextAccessible** aStartContainer,
                                          int32_t* aStartOffset,
                                          HyperTextAccessible** aEndContainer,
                                          int32_t* aEndOffset) {
  TextPoint here(this, aOffset);
  TextPoint end = FindTextPoint(aOffset, eDirNext, eSelectWord, eEndWord);
  if (!end.mContainer || end < here) {
    // If we didn't find a word end, or if we wrapped around (bug 1652833),
    // return with no result.
    return;
  }

  if ((NativeState() & states::EDITABLE) &&
      !(end.mContainer->NativeState() & states::EDITABLE)) {
    // The word search crossed an editable boundary. Return the last word of the
    // editable root.
    return EditableRoot()->LeftWordAt(
        nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT, aStartContainer,
        aStartOffset, aEndContainer, aEndOffset);
  }

  TextPoint start =
      static_cast<HyperTextAccessibleWrap*>(end.mContainer)
          ->FindTextPoint(end.mOffset, eDirPrevious, eSelectWord, eStartWord);

  if (here < start) {
    *aStartContainer = here.mContainer;
    *aEndContainer = start.mContainer;
    *aStartOffset = here.mOffset;
    *aEndOffset = start.mOffset;
  } else {
    *aStartContainer = start.mContainer;
    *aEndContainer = end.mContainer;
    *aStartOffset = start.mOffset;
    *aEndOffset = end.mOffset;
  }
}

void HyperTextAccessibleWrap::NextClusterAt(
    int32_t aOffset, HyperTextAccessible** aNextContainer,
    int32_t* aNextOffset) {
  TextPoint here(this, aOffset);
  TextPoint next =
      FindTextPoint(aOffset, eDirNext, eSelectCluster, eDefaultBehavior);

  if ((next.mOffset == nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT &&
       next.mContainer == Document()) ||
      (next < here)) {
    // If we reached the end of the doc, or if we wrapped to the start of the
    // doc return given offset as-is.
    *aNextContainer = this;
    *aNextOffset = aOffset;
  } else {
    *aNextContainer = next.mContainer;
    *aNextOffset = next.mOffset;
  }
}

void HyperTextAccessibleWrap::PreviousClusterAt(
    int32_t aOffset, HyperTextAccessible** aPrevContainer,
    int32_t* aPrevOffset) {
  TextPoint prev =
      FindTextPoint(aOffset, eDirPrevious, eSelectCluster, eDefaultBehavior);
  *aPrevContainer = prev.mContainer;
  *aPrevOffset = prev.mOffset;
}

void HyperTextAccessibleWrap::RangeOfChild(Accessible* aChild,
                                           int32_t* aStartOffset,
                                           int32_t* aEndOffset) {
  MOZ_ASSERT(aChild->Parent() == this);
  *aStartOffset = *aEndOffset = -1;
  int32_t index = GetIndexOf(aChild);
  if (index != -1) {
    *aStartOffset = GetChildOffset(index);
    // If this is the last child index + 1 will return the total
    // chracter count.
    *aEndOffset = GetChildOffset(index + 1);
  }
}

Accessible* HyperTextAccessibleWrap::LeafAtOffset(int32_t aOffset) {
  HyperTextAccessible* text = this;
  Accessible* child = nullptr;
  int32_t innerOffset = aOffset;
  do {
    int32_t childIdx = text->GetChildIndexAtOffset(innerOffset);
    if (childIdx == -1) {
      return text;
    }

    child = text->GetChildAt(childIdx);
    if (!child || nsAccUtils::MustPrune(child)) {
      return text;
    }

    innerOffset -= text->GetChildOffset(childIdx);

    text = child->AsHyperText();
  } while (text);

  return child;
}

TextPoint HyperTextAccessibleWrap::FindTextPoint(
    int32_t aOffset, nsDirection aDirection, nsSelectionAmount aAmount,
    EWordMovementType aWordMovementType) {
  // Layout can remain trapped in an editable. We normalize out of
  // it if we are in its last offset.
  HyperTextIterator iter(this, aOffset, this, CharacterCount(), true);
  if (aDirection == eDirNext) {
    iter.NormalizeForward();
  } else {
    iter.NormalizeBackward();
  }

  // Find a leaf accessible frame to start with. PeekOffset wants this.
  HyperTextAccessible* text = iter.mCurrentContainer;
  Accessible* child = nullptr;
  int32_t innerOffset = iter.mCurrentStartOffset;

  do {
    int32_t childIdx = text->GetChildIndexAtOffset(innerOffset);

    // We can have an empty text leaf as our only child. Since empty text
    // leaves are not accessible we then have no children, but 0 is a valid
    // innerOffset.
    if (childIdx == -1) {
      NS_ASSERTION(innerOffset == 0 && !text->ChildCount(), "No childIdx?");
      return TextPoint(text, 0);
    }

    child = text->GetChildAt(childIdx);
    if (child->IsHyperText() && !child->ChildCount()) {
      // If this is a childless hypertext, jump to its
      // previous or next sibling, depending on
      // direction.
      if (aDirection == eDirPrevious && childIdx > 0) {
        child = text->GetChildAt(--childIdx);
      } else if (aDirection == eDirNext &&
                 childIdx + 1 < static_cast<int32_t>(text->ChildCount())) {
        child = text->GetChildAt(++childIdx);
      }
    }

    innerOffset -= text->GetChildOffset(childIdx);

    text = child->AsHyperText();
  } while (text);

  nsIFrame* childFrame = child->GetFrame();
  if (!childFrame) {
    NS_ERROR("No child frame");
    return TextPoint(this, aOffset);
  }

  int32_t innerContentOffset = innerOffset;
  if (child->IsTextLeaf()) {
    NS_ASSERTION(childFrame->IsTextFrame(), "Wrong frame!");
    RenderedToContentOffset(childFrame, innerOffset, &innerContentOffset);
  }

  nsIFrame* frameAtOffset = childFrame;
  int32_t offsetInFrame = 0;
  childFrame->GetChildFrameContainingOffset(innerContentOffset, true,
                                            &offsetInFrame, &frameAtOffset);
  if (aDirection == eDirPrevious && offsetInFrame == 0) {
    // If we are searching backwards, and we are at the start of a frame,
    // get the previous continuation frame.
    if (nsIFrame* prevInContinuation = frameAtOffset->GetPrevContinuation()) {
      frameAtOffset = prevInContinuation;
    }
  }

  const bool kIsJumpLinesOk = true;       // okay to jump lines
  const bool kIsScrollViewAStop = false;  // do not stop at scroll views
  const bool kIsKeyboardSelect = true;    // is keyboard selection
  const bool kIsVisualBidi = false;       // use visual order for bidi text
  nsPeekOffsetStruct pos(
      aAmount, aDirection, innerContentOffset, nsPoint(0, 0), kIsJumpLinesOk,
      kIsScrollViewAStop, kIsKeyboardSelect, kIsVisualBidi, false,
      nsPeekOffsetStruct::ForceEditableRegion::No, aWordMovementType, false);
  nsresult rv = frameAtOffset->PeekOffset(&pos);

  // PeekOffset fails on last/first lines of the text in certain cases.
  if (NS_FAILED(rv) && aAmount == eSelectLine) {
    pos.mAmount = aDirection == eDirNext ? eSelectEndLine : eSelectBeginLine;
    frameAtOffset->PeekOffset(&pos);
  }
  if (!pos.mResultContent) {
    NS_ERROR("No result content!");
    return TextPoint(this, aOffset);
  }

  if (aDirection == eDirNext &&
      nsContentUtils::PositionIsBefore(pos.mResultContent, mContent, nullptr,
                                       nullptr)) {
    // Bug 1652833 makes us sometimes return the first element on the doc.
    return TextPoint(Document(), nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT);
  }

  HyperTextAccessible* container =
      nsAccUtils::GetTextContainer(pos.mResultContent);
  int32_t offset = container ? container->DOMPointToOffset(
                                   pos.mResultContent, pos.mContentOffset,
                                   aDirection == eDirNext)
                             : 0;
  return TextPoint(container, offset);
}

HyperTextAccessibleWrap* HyperTextAccessibleWrap::EditableRoot() {
  Accessible* editable = nullptr;
  for (Accessible* acc = this; acc && acc != Document(); acc = acc->Parent()) {
    if (acc->NativeState() & states::EDITABLE) {
      editable = acc;
    } else {
      break;
    }
  }

  return static_cast<HyperTextAccessibleWrap*>(editable->AsHyperText());
}
