-- ic_directory.lua: builds a FreeSWITCH directory user on demand.
-- App users (through Janus, profile "internal"): any name in ic.local that matches
-- the IC rules (lowercase, starts with a letter, letters/digits/._-, 2-32 chars)
-- gets password 1234.
-- SIP phones (e.g. MizuDroid, profile "phones"): only names that have their own
-- file directory/default/<name>.xml, with that file's password. A name with its
-- own file is never generated here, so its file always decides.
-- "not found" makes FreeSWITCH fall back to its normal XML files.
local DOMAIN         = "ic.local"
local PASSWORD       = "1234"
local USER_FILES     = "/usr/local/freeswitch/conf/directory/default/"
local PHONES_PROFILE = "phones"

local function not_found()
  XML_STRING = [[<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<document type="freeswitch/xml">
  <section name="result"><result status="not found"/></section>
</document>]]
end

local user    = params:getHeader("user")
local domain  = params:getHeader("domain") or XML_REQUEST["key_value"]
local profile = params:getHeader("sip_profile")

if XML_REQUEST["section"] ~= "directory" or not user or domain ~= DOMAIN
   or #user > 32 or not user:match("^[a-z][a-z0-9._%-]+$") then
  not_found()
  return
end

freeswitch.consoleLog("info", string.format("ic_directory: user=%s profile=%s action=%s\n",
  user, tostring(profile), tostring(params:getHeader("action"))))

-- A name with its own user file keeps that file (and its password)
local f = io.open(USER_FILES .. user .. ".xml", "r")
if f then
  f:close()
  not_found()
  return
end

-- The public phones profile accepts only names that have their own file
if profile == PHONES_PROFILE then
  not_found()
  return
end

XML_STRING = string.format([[<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<document type="freeswitch/xml">
  <section name="directory">
    <domain name="%s">
      <params>
        <param name="dial-string" value="{^^:sip_invite_domain=${dialed_domain}:presence_id=${dialed_user}@${dialed_domain}}${sofia_contact(*/${dialed_user}@${dialed_domain})}"/>
      </params>
      <groups><group name="default"><users>
        <user id="%s">
          <params>
            <param name="password" value="%s"/>
          </params>
          <variables>
            <variable name="user_context" value="default"/>
            <variable name="effective_caller_id_name" value="%s"/>
            <variable name="effective_caller_id_number" value="%s"/>
          </variables>
        </user>
      </users></group></groups>
    </domain>
  </section>
</document>]], DOMAIN, user, PASSWORD, user, user)
