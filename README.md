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
    [*] --> InitUI: Setup ncurses, timeout(100)
    InitUI --> SelectNode: Choose from configured nodes
    SelectNode --> InitAutoRead: Setup auto-read timer
    InitAutoRead --> MainLoop: Enter main UI loop
    
    state MainLoop {
        [*] --> ShowMenu: Display command menu
        ShowMenu --> HandleInput: Process user input (100ms timeout)
        
        HandleInput --> CheckSystemState: Check busy/waiting flags
        HandleInput --> Navigate: UP/DOWN keys
        HandleInput --> SendCommand: ENTER key
        
        CheckSystemState --> AutoRead: System idle + timer expired
        CheckSystemState --> PauseTimer: System busy or waiting response
        CheckSystemState --> ShowMenu: System idle but timer not expired
        
        PauseTimer --> ShowMenu: Display "timer paused" message
        AutoRead --> ShowMenu: Silent data collection
        Navigate --> UpdateHighlight: Change menu selection
        UpdateHighlight --> ShowMenu: Redraw menu
        
        SendCommand --> CheckBusyWaiting: System busy or waiting?
        CheckBusyWaiting --> ShowBusyMessage: Display busy/waiting message
        CheckBusyWaiting --> ExecuteCommand: Send to UART thread
        
        ShowBusyMessage --> ShowMenu: Continue loop
        ExecuteCommand --> CheckExit: Exit command?
        CheckExit --> ShowMenu: Continue loop
        CheckExit --> [*]: Exit selected
    }
    
    MainLoop --> Cleanup: Exit command received
    Cleanup --> [*]: Close UI, cleanup resources
    
    note right of MainLoop
        Key Features:
        - Auto-read pauses during BUSY state
        - Auto-read pauses during WAITING_RESPONSE state
        - Timer compensation after busy/wait periods
        - Real-time status: IDLE/ACTIVE/BUSY/WAITING
        - Config-driven node selection and menus
        - Thread-safe state tracking with transitions
    end note
```

### Communication Thread State Machine
```mermaid
stateDiagram-v2
    [*] --> Initialize: Enter main loop
    Initialize --> WaitNodeSelection: Check if node selected
    
    WaitNodeSelection --> NodeReady: Node type available
    WaitNodeSelection --> SleepRetry: No node selected yet
    SleepRetry --> WaitNodeSelection: Sleep 100ms, retry
    
    NodeReady --> MainLoop: Enter command processing loop
    
    state MainLoop {
        [*] --> WaitCommand: Wait for command or timeout
        WaitCommand --> CheckCommandType: Command received
        WaitCommand --> WaitCommand: Timeout, retry
        
        CheckCommandType --> AutoRead: AUTO_READ_COMMAND
        CheckCommandType --> ManualCommand: User command
        
        AutoRead --> SilentExecute: Execute quietly by node type
        SilentExecute --> UpdateMQTT: Share data with MQTT thread
        UpdateMQTT --> WaitCommand: Continue loop
        
        ManualCommand --> SetBusyFlag: Set is_busy if blocking command
        SetBusyFlag --> ExecuteByNode: Process command by node type
        ExecuteByNode --> UpdateUIStatus: Update status_response/receive_data
        UpdateUIStatus --> ClearBusyFlag: Clear is_busy flag
        ClearBusyFlag --> WaitCommand: Continue loop
    }
    
    note right of MainLoop
        Key Features:
        - Auto-read: Silent data collection for MQTT
        - Manual commands: Full UI feedback
        - Node1: 9 commands (directions, LED, status, reflash)
        - Node2: 4 commands (LED, read, reflash)
        - Thread-safe: Mutex protection for shared data
        - Non-blocking: Auto-read doesn't block UI
    end note

```

### MQTT Send to thingsboard Thread State Machine:
```mermaid
stateDiagram-v2
    [*] --> Initialize: Setup MQTT client & callbacks
    Initialize --> Connect: Connect to ThingsBoard
    Connect --> WaitConnection: Wait for connection (5s timeout)
    
    WaitConnection --> Connected: Connection successful
    WaitConnection --> Failed: Connection timeout/failed
    
    Connected --> MainLoop: Enter main publishing loop
    
    state MainLoop {
        [*] --> CheckTimer: Check if 1 second elapsed
        CheckTimer --> CollectData: Time to publish
        CheckTimer --> Sleep: Not time yet
        
        CollectData --> GetNodeData: Get Node1 & Node2 data
        GetNodeData --> PublishTelemetry: Send to ThingsBoard
        PublishTelemetry --> VerifyConnection: Check connection status
        
        Sleep --> VerifyConnection: Sleep 100ms
        
        VerifyConnection --> CheckTimer: Still connected
        VerifyConnection --> Reconnect: Connection lost
        
        Reconnect --> ReconnectAttempt: mosquitto_reconnect()
        ReconnectAttempt --> ReconnectDelay: Sleep 1000ms
        ReconnectDelay --> CheckTimer: Continue loop
    }
    
    Failed --> Cleanup: Clean up resources
    Cleanup --> [*]: Thread exit
    
    note right of MainLoop
        Key Features:
        - Publish every 1 second
        - Auto-reconnect on connection loss
        - Callbacks: on_connect, on_disconnect, on_publish
        - Data from: Raw data as hex strings in JSON
    end note

```
