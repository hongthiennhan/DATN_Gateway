### Sơ đồ hệ thống (sơ đồ chức năng):
![Function diagram](./images/Task_1_Function_diagram.png)

## Sơ đồ State Machine Hệ thống

### Main Thread Activity Diagram
```mermaid
stateDiagram-v2
    [*] --> RegisterCleanup: Register cleanup_on_exit()
    RegisterCleanup --> LoadConfig: Load main config.json
    LoadConfig --> FallbackConfig: Load failed
    LoadConfig --> GetDefaults: Load success
    
    FallbackConfig --> LoadNodesConfig: Try nodes_config.json fallback
    LoadNodesConfig --> GetDefaults: Success
    LoadNodesConfig --> [*]: Failed - Exit with error
    
    GetDefaults --> InitData: Get baudrate, device from config
    InitData --> AllocCommandData: Allocate command_data memory
    AllocCommandData --> InitModbus: Initialize Modbus by default
    
    InitModbus --> CreateUARTThread: Create UART thread
    CreateUARTThread --> CreateUIThread: Create UI thread with args
    CreateUIThread --> CreateMQTTThread: Create MQTT thread
    CreateMQTTThread --> CreateModbusThread: Create Modbus thread
    CreateModbusThread --> CreateCANThread: Create CAN thread
    
    CreateCANThread --> WaitThreads: pthread_join all threads
    WaitThreads --> Cleanup: Free device string
    Cleanup --> [*]: Program exit via cleanup_on_exit()
    
    note right of InitModbus
        Default communication is Modbus,
        not UART as shown in original diagram.
        UI allows switching between UART/Modbus/CAN.
    end note
```

### UI (Controller / Configarator) Thread Activity Diagram
```mermaid
stateDiagram-v2
    [*] --> InitNCurses: Initialize ncurses + colors
    InitNCurses --> PauseThreads: Pause UART + Modbus threads
    PauseThreads --> MainMenuLoop: Enter main menu loop
    
    state MainMenuLoop {
        [*] --> DrawHeader: Display header + system info
        DrawHeader --> DrawMenu: Display 7 menu options
        DrawMenu --> ShowStatus: Show status + last data
        ShowStatus --> WaitInput: Wait for key input (100ms timeout)
        
        WaitInput --> HandleUp: KEY_UP
        WaitInput --> HandleDown: KEY_DOWN  
        WaitInput --> HandleEnter: ENTER key
        WaitInput --> HandleQuit: 'q'/'Q' key
        WaitInput --> HandleRefresh: 'r'/'R' key
        WaitInput --> DrawHeader: Timeout - Auto refresh
        
        HandleUp --> DrawHeader: Move highlight up
        HandleDown --> DrawHeader: Move highlight down
        HandleRefresh --> DrawHeader: Force refresh
        HandleQuit --> ExitProgram: End ncurses + exit(0)
        
        state HandleEnter {
            [*] --> CheckMenuIndex: Check selected option (0-6)
            
            CheckMenuIndex --> ViewSystemStatus: Option 0
            ViewSystemStatus --> ShowSystemInfo: Display firmware, device, nodes
            ShowSystemInfo --> WaitAnyKey: Press any key to continue
            
            CheckMenuIndex --> ViewCommConfig: Option 1  
            ViewCommConfig --> ShowMQTTConfig: Display broker, topics, QoS
            ShowMQTTConfig --> WaitAnyKey
            
            CheckMenuIndex --> SelectCommType: Option 2
            SelectCommType --> CommTypeMenu: Show MQTT/HTTP/WebSocket/TCP
            CommTypeMenu --> SetMQTT: Only MQTT implemented
            CommTypeMenu --> NotImplemented: Others not implemented
            SetMQTT --> WaitAnyKey
            NotImplemented --> WaitAnyKey
            
            CheckMenuIndex --> ReloadConfig: Option 3
            ReloadConfig --> CallSafeReload: safe_reload_config()
            CallSafeReload --> ShowReloadResult: Success/Failure message
            ShowReloadResult --> WaitAnyKey
            
            CheckMenuIndex --> ViewNodeConfig: Option 4
            ViewNodeConfig --> ShowNodeList: Display nodes + detection status
            ShowNodeList --> ShowLastDataTime: Show last data received time
            ShowLastDataTime --> WaitAnyKey
            
            CheckMenuIndex --> SelectNodeCommType: Option 5
            SelectNodeCommType --> NodeCommMenu: UART/Modbus/CAN USB/CAN CUSTOM
            NodeCommMenu --> InitUART: Select UART
            NodeCommMenu --> InitModbus: Select Modbus  
            NodeCommMenu --> InitCAN: Select CAN
            InitUART --> ResumeUART: Resume UART, pause others
            InitModbus --> ResumeModbus: Resume Modbus, pause others
            InitCAN --> SetCANType: Set CAN_TYPE + Resume CAN
            ResumeUART --> WaitAnyKey
            ResumeModbus --> WaitAnyKey
            SetCANType --> WaitAnyKey
            
            CheckMenuIndex --> ExitProgram: Option 6
            
            WaitAnyKey --> [*]: Return to main menu
        }
        
        ExitProgram --> [*]: End ncurses + exit(0)
    }

```

### UART Communication Thread ACtivity Diagram
```mermaid
stateDiagram-v2
    [*] --> InitUART: UART thread started - passive listening
    InitUART --> UARTMainLoop: Enter main loop
    
    state UARTMainLoop {
        [*] --> CheckPause: Check pause condition
        CheckPause --> WaitResume: Thread paused
        CheckPause --> CheckReload: Thread active
        WaitResume --> CheckPause: Wait for cond signal
        
        CheckReload --> SkipProcessing: Config reloading
        CheckReload --> CheckDetectionTime: Config ready
        SkipProcessing --> SleepUART: Sleep 100ms
        
        CheckDetectionTime --> NodeDetectionXs: Xs elapsed
        CheckDetectionTime --> CheckAutoData: < Xs
        
        NodeDetectionXs --> SendDetectionCmd: For each UART node
        SendDetectionCmd --> ReadDetectionResp: Timeout from JSON config
        ReadDetectionResp --> ValidateResponse: Check expected_response
        ValidateResponse --> MarkDetected: Response matches
        ValidateResponse --> MarkNotDetected: No match/No response
        MarkDetected --> NextNode: Continue loop
        MarkNotDetected --> NextNode
        NextNode --> CheckAutoData: Detection cycle complete
        
        CheckAutoData --> ReadAutoData: UART data available
        CheckAutoData --> CheckServerCmd: No data available
        ReadAutoData --> ProcessAutoData: Assign to first detected UART node
        ProcessAutoData --> UpdateMQTTData: update_mqtt_data_from_response()
        UpdateMQTTData --> UpdateUIStatus: Update status + hex display (max 50 bytes)
        UpdateUIStatus --> CheckServerCmd
        
        CheckServerCmd --> ExecuteServerCmd: Control command pending
        CheckServerCmd --> SleepUART: No command
        ExecuteServerCmd --> SendUARTCmd: hex_string_to_uint8 + UART_Write_Command
        SendUARTCmd --> SleepUART: No response read for server commands
        
        SleepUART --> CheckPause: Sleep 100ms
    }

```

### MODBUS Communication Thread Activity Diagram
```mermaid
stateDiagram-v2
    [*] --> InitModbus: Modbus thread started - active polling
    InitModbus --> ModbusMainLoop: Enter main loop
    
    state ModbusMainLoop {
        [*] --> CheckModbusPause: Check pause condition
        CheckModbusPause --> WaitModbusResume: Thread paused
        CheckModbusPause --> CheckModbusReload: Thread active
        WaitModbusResume --> CheckModbusPause: Wait for cond signal
        
        CheckModbusReload --> SkipModbusProcessing: Config reloading
        CheckModbusReload --> CheckModbusTime: Config ready
        SkipModbusProcessing --> SleepModbus: Sleep 1000ms during reload
        
        CheckModbusTime --> ModbusPolling1s: 1s elapsed
        CheckModbusTime --> CheckModbusServerCmd: < 1s
        
        ModbusPolling1s --> BuildModbusFrame: For each Modbus node
        BuildModbusFrame --> SendModbusFrame: Modbus_Write_Frame()
        SendModbusFrame --> ReadModbusResp: Modbus_Read_Response()
        ReadModbusResp --> ProcessModbusData: Format display + count
        ProcessModbusData --> UpdateModbusMQTT: update_mqtt_data_from_response()
        UpdateModbusMQTT --> UpdateModbusUI: Update status + receive counter
        UpdateModbusUI --> NextModbusNode: Sleep 5ms between nodes
        NextModbusNode --> CheckModbusServerCmd: Polling complete
        
        CheckModbusServerCmd --> ExecuteModbusCmd: Control command pending
        CheckModbusServerCmd --> SleepModbus: No command
        ExecuteModbusCmd --> SendModbusServerCmd: hex_string_to_bytes + Modbus_Write_Frame
        SendModbusServerCmd --> SleepModbus: No response read for server commands
        
        SleepModbus --> CheckModbusPause: Sleep 100ms
    }

```

### CAN Communication Thread Acitivity Diagram
```mermaid
stateDiagram-v2
    [*] --> InitCAN: CAN thread started - mixed mode
    InitCAN --> CANMainLoop: Enter main loop
    
    state CANMainLoop {
        [*] --> CheckCANPause: Check pause condition
        CheckCANPause --> WaitCANResume: Thread paused
        CheckCANPause --> CheckCANReload: Thread active
        WaitCANResume --> CheckCANPause: Wait for cond signal
        
        CheckCANReload --> SkipCANProcessing: Config reloading
        CheckCANReload --> CheckCANDetectionTime: Config ready
        SkipCANProcessing --> SleepCAN: Sleep 100ms during reload
        
        CheckCANDetectionTime --> CANDetection2s: 2s elapsed
        CheckCANDetectionTime --> CheckCANAutoData: < 2s
        
        CANDetection2s --> ParseCANCommand: For each CAN node
        ParseCANCommand --> BuildCANFrame: USB/Custom format based on CAN_TYPE
        BuildCANFrame --> SendCANFrame: CAN_Write_Data()
        SendCANFrame --> ReadCANResp: CAN_Read_Response()
        ReadCANResp --> ProcessCANData: Mark detected + update MQTT
        ProcessCANData --> UpdateCANUI: Update status + hex display
        UpdateCANUI --> NextCANNode: Sleep 5ms between nodes
        NextCANNode --> CheckCANAutoData: Detection complete
        
        CheckCANAutoData --> ReadCANAutoData: CAN data available
        CheckCANAutoData --> CheckCANServerCmd: No data available
        ReadCANAutoData --> ProcessCANAutoData: Assign to first detected CAN node
        ProcessCANAutoData --> UpdateCANMQTTData: update_mqtt_data_from_response()
        UpdateCANMQTTData --> UpdateCANStatus: Update status display
        UpdateCANStatus --> CheckCANServerCmd
        
        CheckCANServerCmd --> ExecuteCANCmd: Control command pending
        CheckCANServerCmd --> SleepCAN: No command
        ExecuteCANCmd --> BuildCANServerCmd: Build CAN frame for server command
        BuildCANServerCmd --> SendCANServerCmd: CAN_Write_Data()
        SendCANServerCmd --> SleepCAN: No response read
        
        SleepCAN --> CheckCANPause: Sleep 100ms
    }
```


### MQTT Send to thingsboard Thread State Machine:
```mermaid
stateDiagram-v2
    [*] --> LoadMQTTConfig: Load MQTT config
    LoadMQTTConfig --> NoMQTTConfig: Config missing
    LoadMQTTConfig --> InitMosquitto: Config available
    
    NoMQTTConfig --> [*]: Exit thread
    
    InitMosquitto --> CreateMosquittoClient: mosquitto_new()
    CreateMosquittoClient --> ClientFailed: Creation failed
    CreateMosquittoClient --> SetCredentials: mosquitto_username_pw_set()
    
    ClientFailed --> [*]: Exit thread
    
    SetCredentials --> SetCallbacks: Set connect/disconnect/message callbacks
    SetCallbacks --> ConnectBroker: mosquitto_connect()
    ConnectBroker --> ConnectFailed: Connection failed
    ConnectBroker --> StartLoop: mosquitto_loop_start()
    
    ConnectFailed --> MQTTCleanup: Destroy client + cleanup
    MQTTCleanup --> [*]
    
    StartLoop --> WaitConnection: Wait for mqtt_connected flag
    WaitConnection --> ConnectionTimeout: Timeout expired
    WaitConnection --> RequestInitialConfig: Connected successfully
    
    ConnectionTimeout --> StopLoop: mosquitto_loop_stop
    StopLoop --> MQTTCleanup
    
    RequestInitialConfig --> ConfigRequest: request_config_json_robust()
    ConfigRequest --> WaitConfigResponse: Sleep 3s for response
    WaitConfigResponse --> CheckConfigReload: check_and_reload_config()
    CheckConfigReload --> AllocBuffers: Allocate telemetry + status buffers
    AllocBuffers --> BufferFailed: Allocation failed
    AllocBuffers --> MainMQTTLoop: Enter simplified main loop
    
    BufferFailed --> StopLoop
    
    state MainMQTTLoop {
        [*] --> CheckMQTTPause: Check pause condition
        CheckMQTTPause --> WaitMQTTResume: Thread paused
        CheckMQTTPause --> CheckConfigUpdate: Thread active
        WaitMQTTResume --> CheckMQTTPause: Wait for cond signal
        
        CheckConfigUpdate --> ReloadIfNeeded: check_and_reload_config()
        ReloadIfNeeded --> CheckPublishTime: Config checked
        
        CheckPublishTime --> PublishTelemetry: publish_interval elapsed
        CheckPublishTime --> CheckStatusTime: < publish_interval
        PublishTelemetry --> BuildTelemetryPayload: build_telemetry_payload()
        BuildTelemetryPayload --> SendTelemetry: mosquitto_publish(topic_telemetry)
        SendTelemetry --> UpdatePublishTime: last_publish = current_time
        UpdatePublishTime --> CheckStatusTime
        
        CheckStatusTime --> SendStatusUpdate: 60s elapsed
        CheckStatusTime --> CheckReconnection: < 60s
        SendStatusUpdate --> BuildStatusPayload: JSON status + timestamp + node count
        BuildStatusPayload --> PublishStatus: mosquitto_publish(topic_status)
        PublishStatus --> UpdateStatusTime: last_status = current_time
        UpdateStatusTime --> CheckReconnection
        
        CheckReconnection --> HandleReconnect: !mqtt_connected
        CheckReconnection --> SleepMQTT: Connected
        HandleReconnect --> ReconnectAttempt: mosquitto_reconnect()
        ReconnectAttempt --> SleepReconnect: Sleep reconnect_delay_ms
        SleepReconnect --> SleepMQTT
        SleepMQTT --> CheckMQTTPause: Sleep loop_interval_ms
    }
    
    note right of MainMQTTLoop
        MQTT Features:
        - Mosquitto library integration
        - ThingsBoard protocol support
        - Telemetry publishing (configurable interval)
        - Status updates every 60 seconds
        - Automatic reconnection with delay
        - Config sync from server via attributes
        - RPC command processing
    end note
    
    note right of RequestInitialConfig
        MQTT Callbacks:
        - on_mqtt_connect: Subscribe topics + publish attributes
        - on_mqtt_message_robust: Handle control commands + config updates
        - on_mqtt_disconnect: Set connection status
        - Robust config reload with atomic file writes
    end note
```
---

### TCP Send to linux server state machine:
```mermaid
stateDiagram-v2
    [*] --> InitMgr : Initialize Manager
    InitMgr --> CreateConn : Create TCP Connection
    CreateConn --> Connected
    Connected --> PreparePayload : Prepare JSON Payload
    PreparePayload --> SendData : Send Data
    SendData --> ReceiveData : Wait and Receive Data
    ReceiveData --> ProcessIncomingData : Handle Incoming Data
    ProcessIncomingData --> CheckConnection : Check Connection Status
    CheckConnection --> Connected : If Alive
    CheckConnection --> Reconnect : If Disconnected
    Reconnect --> CreateConn : Reconnect
    Connected --> Pause : Check for Pause Event
    Pause --> Connected : Resume
    SendData --> Sleep : Sleep before next send
    Sleep --> PreparePayload : Loop

    note left of InitMgr : Initialize TCP Manager
    note right of CreateConn : Connect to Server
    note right of PreparePayload : Build telemetry JSON payload
    note right of ProcessIncomingData : Process control or config commands

```