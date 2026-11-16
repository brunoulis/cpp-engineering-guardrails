/**
 *
 *  @file HttpUtils.cc
 *  @author An Tao
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  https://github.com/an-tao/drogon
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Drogon
 *
 */

#include "HttpUtils.h"
#include <drogon/utils/Utilities.h>
#include <drogon/Cookie.h>
#include <trantor/utils/Logger.h>
#include <filesystem>
#include <limits>
#include <map>
#include <unordered_map>
#include <mutex>

namespace drogon
{
std::optional<std::string_view> internal::HttpHeaderFieldIterator::next()
{
    if (done_)
        return std::nullopt;

    bool quoted = false;
    size_t end = 0;
    for (; end < remaining_.size(); ++end)
    {
        if (remaining_[end] == '\\' && quoted && end + 1 < remaining_.size())
        {
            ++end;
        }
        else if (remaining_[end] == '"')
        {
            quoted = !quoted;
        }
        else if (remaining_[end] == delimiter_ && !quoted)
        {
            break;
        }
    }
    auto field = utils::trim(remaining_.substr(0, end));
    if (end == remaining_.size())
        done_ = true;
    else
        remaining_.remove_prefix(end + 1);
    return field;
}

std::optional<std::pair<std::string_view, std::string_view>> internal::
    HttpHeaderParameterIterator::next()
{
    auto field = fields_.next();
    if (!field)
        return std::nullopt;
    auto equal = field->find('=');
    if (equal == std::string_view::npos)
        return std::pair<std::string_view, std::string_view>{*field, {}};
    return std::pair<std::string_view, std::string_view>{
        utils::trim(field->substr(0, equal)),
        utils::trim(field->substr(equal + 1))};
}

internal::HttpHeaderFieldIterator iterateHeaderField(std::string_view value)
{
    return {value, ','};
}

std::pair<std::string_view, internal::HttpHeaderParameterIterator>
iterateHeaderParameters(std::string_view value)
{
    internal::HttpHeaderFieldIterator fields(value, ';');
    auto master = fields.next().value_or(std::string_view{});
    return {master, internal::HttpHeaderParameterIterator(fields)};
}

bool containsHttpToken(std::string_view value, std::string_view token)
{
    auto fields = iterateHeaderField(value);
    while (auto field = fields.next())
    {
        if (utils::ci_equals(*field, token))
            return true;
    }
    return false;
}

static std::unordered_map<std::string, std::string> customMime;

// https://en.wikipedia.org/wiki/List_of_file_formats
// https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
// https://www.digipres.org/formats/mime-types/
// https://www.iana.org/assignments/media-types/media-types.xhtml
// content type -> list of corresponding mime types, the first being the default
// (more standard) one + the mime type to return in contentTypeToMime() when not
// empty (mainly to return the charset for text types)
