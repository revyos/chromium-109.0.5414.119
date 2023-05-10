// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/style/typography_provider.h"

#include <string>

#include "base/logging.h"
#include "build/build_config.h"
#include "ui/base/default_style.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"

#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#endif

namespace views {
namespace {

gfx::Font::Weight GetValueBolderThan(gfx::Font::Weight weight) {
  switch (weight) {
    case gfx::Font::Weight::BOLD:
      return gfx::Font::Weight::EXTRA_BOLD;
    case gfx::Font::Weight::EXTRA_BOLD:
    case gfx::Font::Weight::BLACK:
      return gfx::Font::Weight::BLACK;
    default:
      return gfx::Font::Weight::BOLD;
  }
}

ui::ColorId GetDisabledColorId(int context) {
  switch (context) {
    case style::CONTEXT_BUTTON_MD:
      return ui::kColorButtonForegroundDisabled;
    case style::CONTEXT_TEXTFIELD:
      return ui::kColorTextfieldForegroundDisabled;
    case style::CONTEXT_MENU:
    case style::CONTEXT_TOUCH_MENU:
      return ui::kColorMenuItemForegroundDisabled;
    default:
      return ui::kColorLabelForegroundDisabled;
  }
}

ui::ColorId GetMenuColorId(int style) {
  switch (style) {
    case style::STYLE_SECONDARY:
      return ui::kColorMenuItemForegroundSecondary;
    case style::STYLE_SELECTED:
      return ui::kColorMenuItemForegroundSelected;
    case style::STYLE_HIGHLIGHTED:
      return ui::kColorMenuItemForegroundHighlighted;
    default:
      return ui::kColorMenuItemForeground;
  }
}

ui::ColorId GetHintColorId(int context) {
  return (context == style::CONTEXT_TEXTFIELD)
             ? ui::kColorTextfieldForegroundPlaceholder
             : ui::kColorLabelForegroundSecondary;
}

ui::ColorId GetColorId(int context, int style) {
  if (style == style::STYLE_DIALOG_BUTTON_DEFAULT)
    return ui::kColorButtonForegroundProminent;
  if (style == style::STYLE_DISABLED)
    return GetDisabledColorId(context);
  if (style == style::STYLE_LINK)
    return ui::kColorLinkForeground;
  if (style == style::STYLE_HINT)
    return GetHintColorId(context);
  if (context == style::CONTEXT_BUTTON_MD)
    return ui::kColorButtonForeground;
  if (context == style::CONTEXT_LABEL && style == style::STYLE_SECONDARY)
    return ui::kColorLabelForegroundSecondary;
  if (context == style::CONTEXT_DIALOG_BODY_TEXT &&
      (style == style::STYLE_PRIMARY || style == style::STYLE_SECONDARY))
    return ui::kColorDialogForeground;
  if (context == style::CONTEXT_TEXTFIELD)
    return ui::kColorTextfieldForeground;
  if (context == style::CONTEXT_MENU || context == style::CONTEXT_TOUCH_MENU)
    return GetMenuColorId(style);
  return ui::kColorLabelForeground;
}

}  // namespace

ui::ResourceBundle::FontDetails TypographyProvider::GetFontDetails(
    int context,
    int style) const {
  DCHECK(StyleAllowedForContext(context, style))
      << "context: " << context << " style: " << style;

  ui::ResourceBundle::FontDetails details;

  switch (context) {
    case style::CONTEXT_BUTTON_MD:
      details.size_delta = ui::kLabelFontSizeDelta;
      details.weight = TypographyProvider::MediumWeightForUI();
      break;
    case style::CONTEXT_DIALOG_TITLE:
      details.size_delta = ui::kTitleFontSizeDelta;
      break;
    case style::CONTEXT_TOUCH_MENU:
      details.size_delta = 2;
      break;
    default:
      details.size_delta = ui::kLabelFontSizeDelta;
      break;
  }

  switch (style) {
    case style::STYLE_TAB_ACTIVE:
      details.weight = gfx::Font::Weight::BOLD;
      break;
    case style::STYLE_DIALOG_BUTTON_DEFAULT:
      // Only non-MD default buttons should "increase" in boldness.
      if (context == style::CONTEXT_BUTTON) {
        details.weight =
            GetValueBolderThan(ui::ResourceBundle::GetSharedInstance()
                                   .GetFontListForDetails(details)
                                   .GetFontWeight());
      }
      break;
    case style::STYLE_EMPHASIZED:
    case style::STYLE_EMPHASIZED_SECONDARY:
      details.weight = gfx::Font::Weight::SEMIBOLD;
      break;
  }

  return details;
}

const gfx::FontList& TypographyProvider::GetFont(int context, int style) const {
  return ui::ResourceBundle::GetSharedInstance().GetFontListForDetails(
      GetFontDetails(context, style));
}

SkColor TypographyProvider::GetColor(const View& view,
                                     int context,
                                     int style) const {
  return view.GetColorProvider()->GetColor(GetColorId(context, style));
}

int TypographyProvider::GetLineHeight(int context, int style) const {
  return GetFont(context, style).GetHeight();
}

bool TypographyProvider::StyleAllowedForContext(int context, int style) const {
  // TODO(https://crbug.com/1352340): Limit emphasizing text to contexts where
  // it's obviously correct. chrome_typography_provider.cc implements this
  // correctly, but that does not cover uses outside of //chrome or //ash.
  return true;
}

// static
gfx::Font::Weight TypographyProvider::MediumWeightForUI() {
#if BUILDFLAG(IS_MAC)
  // System fonts are not user-configurable on Mac, so it's simpler.
  return gfx::Font::Weight::MEDIUM;
#else
  // NORMAL may already have at least MEDIUM weight. Return NORMAL in that case
  // since trying to return MEDIUM would actually make the font lighter-weight
  // than the surrounding text. For example, Windows can be configured to use a
  // BOLD font for dialog text; deriving MEDIUM from that would replace the BOLD
  // attribute with something lighter.
  if (ui::ResourceBundle::GetSharedInstance()
          .GetFontListForDetails(ui::ResourceBundle::FontDetails())
          .GetFontWeight() < gfx::Font::Weight::MEDIUM)
    return gfx::Font::Weight::MEDIUM;
  return gfx::Font::Weight::NORMAL;
#endif
}

}  // namespace views