GET http://192.168.1.110/redfish
HTTP 200
[Asserts]
header "Content-Type" == "application/json"

GET http://192.168.1.110/redfish/
HTTP 200
[Asserts]
header "Content-Type" == "application/json"

GET http://192.168.1.110/redfish/v1
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$.AccountService['@odata.id']" == "/redfish/v1/AccountService"
jsonpath "$.SessionService['@odata.id']" == "/redfish/v1/SessionService"
jsonpath "$.Managers['@odata.id']" == "/redfish/v1/Managers"
jsonpath "$.Systems['@odata.id']" == "/redfish/v1/Systems"
jsonpath "$.Chassis['@odata.id']" == "/redfish/v1/Chassis"
jsonpath "$.Links.Sessions['@odata.id']" == "/redfish/v1/SessionService/Sessions"

GET http://192.168.1.110/redfish/v1/
HTTP 200
[Asserts]
header "Content-Type" == "application/json"

GET http://192.168.1.110/redfish/v1/Systems
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/Systems"
jsonpath "$['@odata.type']" == "#ComputerSystemCollection.ComputerSystemCollection"
jsonpath "$.Name" == "Computer System Collection"
jsonpath "$.Members" count == 1

GET http://192.168.1.110/redfish/v1/Systems/system
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$.Name" == "WallaBMC Board"
jsonpath "$['@odata.id']" == "/redfish/v1/Systems/system"
jsonpath "$['@odata.type']" == "#ComputerSystem.v1_22_0.ComputerSystem"
jsonpath "$.Id" == "system"
jsonpath "$.UUID" == "38947555-7742-3448-3784-823347823834"
jsonpath "$.Name" == "WallaBMC Board"
jsonpath "$.PowerState" exists
jsonpath "$.SerialNumber" == "12345"
jsonpath "$.Actions['#ComputerSystem.Reset']['ResetType@Redfish.AllowableValues'][0]" == "On"

POST http://192.168.1.110/redfish
HTTP 405

POST http://192.168.1.110/redfish/v1
HTTP 405

POST http://192.168.1.110/redfish/v1/Systems
HTTP 405

POST http://192.168.1.110/redfish/v1/Systems/system
HTTP 405

GET https://192.168.1.110/redfish
HTTP 200
[Asserts]
header "Content-Type" == "application/json"

GET https://192.168.1.110/redfish/
HTTP 200
[Asserts]
header "Content-Type" == "application/json"

GET https://192.168.1.110/redfish/v1
HTTP 200
[Asserts]
header "Content-Type" == "application/json"

GET https://192.168.1.110/redfish/v1/
HTTP 200
[Asserts]
header "Content-Type" == "application/json"

GET https://192.168.1.110/redfish/v1/Systems
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/Systems"
jsonpath "$['@odata.type']" == "#ComputerSystemCollection.ComputerSystemCollection"
jsonpath "$.Name" == "Computer System Collection"
jsonpath "$.Members" count == 1

GET https://192.168.1.110/redfish/v1/Systems/system
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$.Name" == "WallaBMC Board"
jsonpath "$['@odata.id']" == "/redfish/v1/Systems/system"
jsonpath "$['@odata.type']" == "#ComputerSystem.v1_22_0.ComputerSystem"
jsonpath "$.Id" == "system"
jsonpath "$.UUID" == "38947555-7742-3448-3784-823347823834"
jsonpath "$.Name" == "WallaBMC Board"
jsonpath "$.PowerState" exists
jsonpath "$.SerialNumber" == "12345"
jsonpath "$.Actions['#ComputerSystem.Reset']['ResetType@Redfish.AllowableValues'][0]" == "On"

POST https://192.168.1.110/redfish
HTTP 405

POST https://192.168.1.110/redfish/v1
HTTP 405

POST https://192.168.1.110/redfish/v1/Systems
HTTP 405

POST https://192.168.1.110/redfish/v1/Systems/system
HTTP 405

# --- Additional endpoint coverage (HTTP) ---

GET http://192.168.1.110/redfish/v1/odata
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.context']" == "/redfish/v1/$metadata"
jsonpath "$.value" count == 5

GET http://192.168.1.110/redfish/v1/$metadata
HTTP 200
[Asserts]
header "Content-Type" == "application/xml"
header "Content-Encoding" == "gzip"

GET http://192.168.1.110/redfish/v1/AccountService
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/AccountService"
jsonpath "$['@odata.type']" == "#AccountService.v1_10_0.AccountService"

GET http://192.168.1.110/redfish/v1/AccountService/Accounts
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/AccountService/Accounts"
jsonpath "$['@odata.type']" == "#ManagerAccountCollection.ManagerAccountCollection"
jsonpath "$.Members" count == 1

GET http://192.168.1.110/redfish/v1/AccountService/Accounts/1
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/AccountService/Accounts/1"
jsonpath "$['@odata.type']" == "#ManagerAccount.v1_9_0.ManagerAccount"
jsonpath "$.Id" == "1"
jsonpath "$.UserName" == "admin"
jsonpath "$.AccountTypes" count == 1
jsonpath "$.AccountTypes[0]" == "Redfish"

GET http://192.168.1.110/redfish/v1/SessionService
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/SessionService"
jsonpath "$['@odata.type']" == "#SessionService.v1_1_9.SessionService"
jsonpath "$.Id" == "SessionService"
jsonpath "$.Sessions['@odata.id']" == "/redfish/v1/SessionService/Sessions"

GET http://192.168.1.110/redfish/v1/SessionService/Sessions
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/SessionService/Sessions"
jsonpath "$['@odata.type']" == "#SessionCollection.SessionCollection"
jsonpath "$.Members" count == 0

GET http://192.168.1.110/redfish/v1/Managers
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/Managers"
jsonpath "$['@odata.type']" == "#ManagerCollection.ManagerCollection"
jsonpath "$.Members" count == 1

GET http://192.168.1.110/redfish/v1/Managers/bmc
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/Managers/bmc"
jsonpath "$['@odata.type']" == "#Manager.v1_11_0.Manager"
jsonpath "$.Id" == "bmc"
jsonpath "$.DateTime" exists
jsonpath "$.FirmwareVersion" exists
jsonpath "$.Oem.WallaBMC['@odata.type']" == "#WallaBMC.v1_0_0.WallaBMC"

GET http://192.168.1.110/redfish/v1/Managers/bmc/EthernetInterfaces
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/Managers/bmc/EthernetInterfaces"
jsonpath "$['@odata.type']" == "#EthernetInterfaceCollection.EthernetInterfaceCollection"
jsonpath "$.Members" count == 1

GET http://192.168.1.110/redfish/v1/Managers/bmc/EthernetInterfaces/eth0
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/Managers/bmc/EthernetInterfaces/eth0"
jsonpath "$['@odata.type']" == "#EthernetInterface.v1_5_0.EthernetInterface"
jsonpath "$.Id" == "eth0"
jsonpath "$.DHCPv4.DHCPEnabled" exists

GET http://192.168.1.110/redfish/v1/Managers/bmc/NetworkProtocol
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.type']" == "#ManagerNetworkProtocol.v1_9_0.ManagerNetworkProtocol"
jsonpath "$.Id" == "NetworkProtocol"

GET http://192.168.1.110/redfish/v1/Chassis
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/Chassis"
jsonpath "$['@odata.type']" == "#ChassisCollection.ChassisCollection"
jsonpath "$.Members" count == 1

GET http://192.168.1.110/redfish/v1/Chassis/1
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/Chassis/1"
jsonpath "$['@odata.type']" == "#Chassis.v1_22_0.Chassis"
jsonpath "$.Id" == "1"
jsonpath "$.PowerState" exists

GET http://192.168.1.110/redfish/v1/Chassis/1/Sensors
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/Chassis/1/Sensors"
jsonpath "$['@odata.type']" == "#SensorCollection.SensorCollection"

# Requires CONFIG_APP_SENSORS (absent on builds without sensor support).
GET http://192.168.1.110/redfish/v1/Chassis/1/Sensors/TempBmc
HTTP 200
[Asserts]
header "Content-Type" == "application/json"
jsonpath "$['@odata.id']" == "/redfish/v1/Chassis/1/Sensors/TempBmc"
jsonpath "$['@odata.type']" == "#Sensor.v1_2_0.Sensor"
jsonpath "$.Id" == "TempBmc"
jsonpath "$.ReadingType" == "Temperature"
jsonpath "$.PhysicalContext" == "Manager"

# --- Error / method handling ---

# GET is not allowed on the reset action (POST only).
GET http://192.168.1.110/redfish/v1/Systems/system/Actions/ComputerSystem.Reset
HTTP 405
[Asserts]
header "Allow" == "POST"

# Valid JSON but an unknown ResetType is rejected without touching power.
POST http://192.168.1.110/redfish/v1/Systems/system/Actions/ComputerSystem.Reset
Content-Type: application/json
```
{ "ResetType": "Bogus" }
```
HTTP 400

# POST is not allowed on a GET-only collection.
POST http://192.168.1.110/redfish/v1/Managers
HTTP 405
[Asserts]
header "Allow" == "GET"

# Unknown resource paths return 404.
GET http://192.168.1.110/redfish/v1/Systems/bogus
HTTP 404
