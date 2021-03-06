/*
 * Json.cpp
 *
 * Copyright (C) 2009-12 by RStudio, Inc.
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#include <core/json/Json.hpp>
#include <core/json/JsonRpc.hpp>

#include <cstdlib>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/regex.hpp>
#include <boost/scoped_array.hpp>

#include <core/Log.hpp>
#include <core/Thread.hpp>

#include "spirit/json_spirit.h"

namespace rstudio {
namespace core {
namespace json {

json_spirit::Value_type ObjectType = json_spirit::obj_type;
json_spirit::Value_type ArrayType = json_spirit::array_type; 
json_spirit::Value_type StringType = json_spirit::str_type;
json_spirit::Value_type BooleanType = json_spirit::bool_type;
json_spirit::Value_type IntegerType = json_spirit::int_type;
json_spirit::Value_type RealType = json_spirit::real_type;
json_spirit::Value_type NullType = json_spirit::null_type;

json::Value toJsonString(const std::string& val)
{
   return json::Value(val);
}

json::Object toJsonObject(
      const std::vector<std::pair<std::string,std::string> >& options)
{
   json::Object optionsJson;
   typedef std::pair<std::string,std::string> Pair;
   BOOST_FOREACH(const Pair& option, options)
   {
      optionsJson[option.first] = option.second;
   }
   return optionsJson;
}

json::Array toJsonArray(const std::vector<std::pair<std::string,std::string> >& options)
{
   json::Array optionsArray;
   typedef std::pair<std::string,std::string> Pair;

   for (const Pair& option : options)
   {
      // escape the equals in the keys and values
      // this is necessary because we will jam the key value pairs together
      // into an array to ensure that options stay ordered, and we want to make sure
      // to properly deliniate between the real equals delimiter and an equals value
      // in the key value pairs
      std::string escapedKey = boost::replace_all_copy(option.first, "=", "\\=");
      std::string escapedValue = boost::replace_all_copy(option.second, "=", "\\=");

      std::string argVal = escapedKey;
      if (!escapedValue.empty())
         argVal += "=" + escapedValue;

      optionsArray.push_back(argVal);
   }

   return optionsArray;
}

std::vector<std::pair<std::string,std::string> > optionsFromJson(const json::Object& optionsJson)
{
   std::vector<std::pair<std::string,std::string> > options;
   BOOST_FOREACH(const json::Member& member, optionsJson)
   {
      std::string name = member.first;
      json::Value value = member.second;
      if (value.type() == json::StringType)
         options.push_back(std::make_pair(name, value.get_str()));
   }
   return options;
}

std::vector<std::pair<std::string,std::string> > optionsFromJson(const json::Array& optionsJson)
{
   std::vector<std::pair<std::string,std::string> > options;
   for (const json::Value& value : optionsJson)
   {
      if (value.type() != json::StringType)
         continue;

      const std::string& optionStr = value.get_str();

      // find the first equals that is not preceded by an escape character
      // this is the actual position in the string we will split on to get
      // the key and value separated
      boost::smatch results;
      boost::regex rx("[^\\\\]=");
      if (boost::regex_search(optionStr, results, rx))
      {
         std::string key = optionStr.substr(0, results.position() + 1);
         std::string value = optionStr.substr(results.position() + 2);
         boost::replace_all(key, "\\=", "=");
         boost::replace_all(value, "\\=", "=");
         options.push_back(std::make_pair(key, value));
      }
      else
      {
         // no value, just a key
         std::string unescapedKey = boost::replace_all_copy(optionStr, "\\=", "=");
         options.push_back(std::make_pair(unescapedKey, std::string()));
      }
   }
   return options;
}

bool fillSetString(const Array& array, std::set<std::string>* pSet)
{
   for (Array::const_iterator it = array.begin();
        it != array.end();
        ++it)
   {
      if (!isType<std::string>(*it))
         return false;
      pSet->insert(it->get_str());
   }

   return true;
}

bool fillVectorString(const Array& array, std::vector<std::string>* pVector)
{
   for (Array::const_iterator it = array.begin();
        it != array.end();
        ++it)
   {
      if (!isType<std::string>(*it))
         return false;
      pVector->push_back(it->get_str());
   }
   
   return true;
}

bool fillVectorInt(const Array& array, std::vector<int>* pVector)
{
   for (Array::const_iterator it = array.begin();
        it != array.end();
        ++it)
   {
      if (!isType<int>(*it))
         return false;
      pVector->push_back(it->get_int());
   }

   return true;
}

bool fillMap(const Object& object, std::map< std::string, std::vector<std::string> >* pMap)
{
   for (Object::const_iterator it = object.begin();
        it != object.end();
        ++it)
   {
      std::vector<std::string> strings;
      const json::Array& array = it->second.get_array();
      if (!fillVectorString(array, &strings))
         return false;
      
      (*pMap)[it->first] = strings;
   }
   return true;
}

bool parse(const std::string& input, Value* pValue)
{
   // two threads simultaneously using the json parser has been observed
   // to crash the process. protect it globally with a mutex. note this was
   // probably a result of not defining BOOST_SPIRIT_THREADSAFE (which we
   // have subsequently defined) however since there isn't much documentation 
   // on the behavior of boost spirit w/ threads and the specific behavior
   // of this constant we leave in mutex just to be sure   
      
   static boost::mutex s_spiritMutex ;
   LOCK_MUTEX(s_spiritMutex)
   {
      return json_spirit::read(input, *pValue);
   }
   END_LOCK_MUTEX
   
   // mutex related error
   return false;
}

void write(const Value& value, std::ostream& os)
{
   json_spirit::write(value, os);
}

void writeFormatted(const Value& value, std::ostream& os)
{
   json_spirit::write_formatted(value, os);
}

std::string write(const Value& value)
{
   return json_spirit::write(value);
}

std::string writeFormatted(const Value& value)
{
   return json_spirit::write_formatted(value);
}

} // namespace json
} // namespace core
} // namespace rstudio



