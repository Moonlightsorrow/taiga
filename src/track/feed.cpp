/*
** Taiga
** Copyright (C) 2010-2017, Eren Okka
** 
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "base/base64.h"
#include "base/html.h"
#include "base/string.h"
#include "base/xml.h"
#include "library/anime_util.h"
#include "taiga/http.h"
#include "taiga/path.h"
#include "track/feed.h"
#include "track/recognition.h"

GenericFeedItem::GenericFeedItem()
    : permalink(true) {
}

FeedItem::FeedItem()
    : state(FeedItemState::Blank) {
}

void FeedItem::Discard(int option) {
  switch (option) {
    default:
    case kFeedFilterOptionDefault:
      state = FeedItemState::DiscardedNormal;
      break;
    case kFeedFilterOptionDeactivate:
      state = FeedItemState::DiscardedInactive;
      break;
    case kFeedFilterOptionHide:
      state = FeedItemState::DiscardedHidden;
      break;
  }
}

bool FeedItem::IsDiscarded() const {
  switch (state) {
    case FeedItemState::DiscardedNormal:
    case FeedItemState::DiscardedInactive:
    case FeedItemState::DiscardedHidden:
      return true;
    default:
      return false;
  }
}

bool FeedItem::operator<(const FeedItem& item) const {
  // Initialize priority list
  static const int state_priorities[] = {1, 2, 3, 4, 0};

  // Sort items by the priority of their state
  return state_priorities[static_cast<size_t>(this->state)] <
         state_priorities[static_cast<size_t>(item.state)];
}

bool FeedItem::operator==(const FeedItem& item) const {
  // Check for guid element first
  if (permalink && item.permalink)
    if (!guid.empty() || !item.guid.empty())
      if (guid == item.guid)
        return true;

  // Fall back to link element
  if (!link.empty() || !item.link.empty())
    if (link == item.link)
      return true;

  // Fall back to title element
  if (!title.empty() || !item.title.empty())
    if (title == item.title)
      return true;

  // Items are different
  return false;
}

TorrentCategory FeedItem::GetTorrentCategory() const {
  if (InStr(category, L"Batch") > -1)  // Respect feed's own categorization
    return TorrentCategory::Batch;

  if (Meow.IsBatchRelease(episode_data))
    return TorrentCategory::Batch;

  if (!Meow.IsValidAnimeType(episode_data))
    return TorrentCategory::Other;

  if (anime::IsEpisodeRange(episode_data))
    return TorrentCategory::Batch;

  if (!episode_data.file_extension().empty())
    if (!Meow.IsValidFileExtension(episode_data.file_extension()))
      return TorrentCategory::Other;

  return TorrentCategory::Anime;
}

////////////////////////////////////////////////////////////////////////////////

Feed::Feed()
    : category(FeedCategory::Link) {
}

std::wstring Feed::GetDataPath() {
  std::wstring path = taiga::GetPath(taiga::Path::Feed);

  if (!link.empty()) {
    Url url(link);
    path += Base64Encode(url.host, true) + L"\\";
  }

  return path;
}

bool Feed::Load() {
  items.clear();

  xml_document document;
  std::wstring file = GetDataPath() + L"feed.xml";
  const unsigned int options = pugi::parse_default | pugi::parse_trim_pcdata;
  xml_parse_result parse_result = document.load_file(file.c_str(), options);

  if (parse_result.status != pugi::status_ok)
    return false;

  Load(document);
  return true;
}

bool Feed::Load(const std::wstring& data) {
  items.clear();

  xml_document document;
  xml_parse_result parse_result = document.load(data.c_str());

  if (parse_result.status != pugi::status_ok)
    return false;

  Load(document);
  return true;
}

void Feed::Load(const xml_document& document) {
  // Read channel information
  xml_node channel = document.child(L"rss").child(L"channel");
  title = XmlReadStrValue(channel, L"title");
  link = XmlReadStrValue(channel, L"link");
  description = XmlReadStrValue(channel, L"description");

  // Read items
  foreach_xmlnode_(node, channel, L"item") {
    // Read data
    FeedItem item;
    item.category = XmlReadStrValue(node, L"category");
    item.title = XmlReadStrValue(node, L"title");
    item.link = XmlReadStrValue(node, L"link");
    item.description = XmlReadStrValue(node, L"description");
    item.category = XmlReadStrValue(node, L"category");
    item.guid = XmlReadStrValue(node, L"guid");
    item.pub_date = XmlReadStrValue(node, L"pubDate");

    xml_node enclosure_node = node.child(L"enclosure");
    if (!enclosure_node.empty()) {
      item.enclosure_url = enclosure_node.attribute(L"url").value();
      item.enclosure_length = enclosure_node.attribute(L"length").value();
      item.enclosure_type = enclosure_node.attribute(L"type").value();
    }

    for (auto element : node.children()) {
      if (InStr(element.name(), L":") > -1) {
        item.elements[element.name()] = element.child_value();
      }
    }

    auto permalink = XmlReadStrValue(node, L"isPermaLink");
    if (!permalink.empty())
      item.permalink = ToBool(permalink);

    if (category == FeedCategory::Link)
      if (item.title.empty() || item.link.empty())
        continue;

    DecodeHtmlEntities(item.title);
    DecodeHtmlEntities(item.description);

    Aggregator.ParseFeedItem(link, item);
    Aggregator.CleanupDescription(item.description);

    items.push_back(item);
  }
}