### Sơ đồ hệ thống (sơ đồ chức năng):
![Function diagram](./images/Task_1_Function_diagram.png)

## Sơ đồ State Machine Hệ thống

### Main Thread State Machine
```mermaid
stateDiagram-v2
    [*] --> ParseArgs: Parse command line arguments
    ParseArgs --> LoadNodeConfig: Load configuration
    LoadNodeConfig --> InitUART: Initialize UART connection
    InitUART --> CreateUIThread: Create UI Thread
    CreateUIThread --> CreateUARTThread: Create UART Thread
    CreateUARTThread --> MQTTThread: Create MQTT Thread
    MQTTThread --> WaitThread: Wait for threads to finish
    WaitThread --> Cleanup: Close UART and cleanup
    Cleanup --> [*]: Program exit
```

### UI (Controller / Configarator) Thread State Machine
```mermaid
stateDiagram-v2
    [*] --> InitUI: Initialize ncurses + setup colors
    
    InitUI --> MainMenuLoop: Enter main menu loop
    
    state MainMenuLoop {
        [*] --> DrawScreen: Render header, system info, menu options
        DrawScreen --> WaitKey: Wait for user input (timeout = refresh delay)
        
        WaitKey --> HandleKeyUp: KEY_UP pressed
        WaitKey --> HandleKeyDown: KEY_DOWN pressed
        WaitKey --> HandleEnter: ENTER pressed
        WaitKey --> HandleQuit: 'q' or 'Q' pressed
        WaitKey --> RefreshLoop: 'r' or 'R' pressed
        WaitKey --> DrawScreen: Timeout, auto-refresh
        
        HandleKeyUp --> DrawScreen: Move highlight ↑
        HandleKeyDown --> DrawScreen: Move highlight ↓
        RefreshLoop --> DrawScreen: Redraw menu
        
        state HandleEnter {
            [*] --> CheckOption: Evaluate selected menu index
            
            CheckOption --> ViewSystem: Option 0
            ViewSystem --> WaitKey: Show firmware, device info
            
            CheckOption --> ViewCommConfig: Option 1
            ViewCommConfig --> WaitKey: Show MQTT config
            
            CheckOption --> SelectCommType: Option 2
            SelectCommType --> ChangeCommMQTT: Select MQTT (implemented)
            SelectCommType --> NotImplemented: Other types (HTTP/WebSocket/TCP)
            ChangeCommMQTT --> WaitKey
            NotImplemented --> WaitKey
            
            CheckOption --> ReloadConfig: Option 3
            ReloadConfig --> WaitKey: Reload JSON configs
            
            CheckOption --> ViewNodeConfig: Option 4
            ViewNodeConfig --> WaitKey: Show node list + last data time
            
            CheckOption --> Exit: Option 5
            Exit --> [*]: End ncurses + exit program
        }
        
        HandleQuit --> [*]: End ncurses + exit program
    }
    
    note right of MainMenuLoop
        Key Features:
        - ncurses UI with highlight colors
        - Menu navigation (↑↓ ENTER)
        - System info (UART, baudrate, comm type)
        - Config reload from JSON
        - Node status with last data received
        - Select Communication Type (only MQTT implemented)
        - Safe thread access (mutex on status/receive_data)
    end note

```

### Communication Thread State Machine
```mermaid
stateDiagram-v2
    [*] --> InitUART: UART thread started
    InitUART --> UARTLoop: Enter UART listener loop

    state UARTLoop {
        [*] --> PeriodicDetection: Check periodic node detection
        PeriodicDetection --> AutoDataCheck: Check UART data available

        AutoDataCheck --> ReadAutoData: Data available
        AutoDataCheck --> ControlCmdCheck: No data available

        ReadAutoData --> ProcessData: Parse and process node data
        ProcessData --> UpdateStatus: Update status + last data
        UpdateStatus --> FreeBuffer: Free buffer
        FreeBuffer --> ControlCmdCheck

        ControlCmdCheck --> ExecuteCmd: Server control command pending
        ControlCmdCheck --> SleepLoop: No command

        ExecuteCmd --> SleepLoop: Send UART command (no response read)

        SleepLoop --> PeriodicDetection: Sleep 100ms, retry
    }

    note right of UARTLoop
        Key Features:
        - Periodic node detection by interval
        - Automatic UART data reception
        - Data processing + status update
        - Hex dump (max 50 bytes) for UI display
        - Handle server-issued commands (non-blocking)
        - Loop with 100ms sleep to reduce CPU usage
    end note
```

### MQTT Send to thingsboard Thread State Machine:
```mermaid
stateDiagram-v2
    [*] --> InitMQTTConfig: Load MQTT config
    InitMQTTConfig --> NoConfig: Config missing
    InitMQTTConfig --> InitMQTT: Config available
    
    NoConfig --> [*]: Exit thread
    
    InitMQTT --> CreateClient: mosquitto_new()
    CreateClient --> SetCallbacks: Set username, password, callbacks
    SetCallbacks --> ConnectBroker: Connect to MQTT broker
    ConnectBroker --> ConnectFail: Connection failed
    ConnectBroker --> WaitConnection: Connection success
    
    ConnectFail --> CleanupExit: Destroy client + cleanup
    CleanupExit --> [*]
    
    WaitConnection --> ConnectionTimeout: Timeout expired
    WaitConnection --> RequestInitialConfig: Connected
    
    ConnectionTimeout --> CleanupExit
    
    RequestInitialConfig --> ReloadConfig: Request config JSON
    ReloadConfig --> MainLoop: Enter main publish loop
    
    state MainLoop {
        [*] --> CheckConfig: Check config updates
        CheckConfig --> PublishTelemetry: Time to publish telemetry
        CheckConfig --> SendStatus: Time to send status
        CheckConfig --> HandleReconnect: If disconnected
        CheckConfig --> SleepLoop: Otherwise
        
        PublishTelemetry --> CheckConfig
        SendStatus --> CheckConfig
        HandleReconnect --> CheckConfig
        SleepLoop --> CheckConfig: Sleep loop_interval_ms
    }
    
    note right of MainLoop
        Key Features:
        - MQTT telemetry publishing (config.publish_interval)
        - Periodic status message (every 60s)
        - Robust config reload
        - Automatic reconnect on disconnection
        - Loop with sleep interval (loop_interval_ms)
    end note
```
---

