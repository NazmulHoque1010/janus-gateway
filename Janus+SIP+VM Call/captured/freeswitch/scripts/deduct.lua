local uuid = argv[1]
local ext  = argv[2]
local api  = freeswitch.API()

local ans = tonumber(api:executeString("uuid_getvar " .. uuid .. " answer_epoch")) or 0
local secs = 0
if ans > 0 then
  secs = os.time() - ans
end

freeswitch.consoleLog("notice", "deduct ext=" .. ext .. " answer_epoch=" .. ans .. " secs=" .. secs .. "\n")

if secs > 0 then
  api:executeString("curl http://127.0.0.1:8080/?action=deduct&ext=" .. ext .. "&seconds=" .. secs)
end
