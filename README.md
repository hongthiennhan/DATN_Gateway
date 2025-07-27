### Sơ đồ hệ thống (sơ đồ chức năng):
![Function diagram](./images/Task_1_Function_diagram.png)

## Sơ đồ State Machine Hệ thống

### Main Thread State Machine
```mermaid
stateDiagram-v2
    [*] --> ParseArgs: Parse command line arguments
    ParseArgs --> LoadConfig: Load configuration
    LoadConfig --> InitUART: Initialize UART connection
    InitUART --> CreateUIThread: Create UI Thread
    CreateUIThread --> CreateUARTThread: Create UART Thread
    CreateUARTThread --> WaitThreads: Wait for threads to finish
    WaitThreads --> Cleanup: Close UART and cleanup
    Cleanup --> [*]: Program exit
```
#### Bản đơn giản:
```mermaid
stateDiagram-v2
    [*] --> Init: Initialize system
    Init --> CreateThreads: Create UI & UART threads
    CreateThreads --> Wait: Wait for threads
    Wait --> Cleanup: Cleanup resources
    Cleanup --> [*]: Exit
```

### UI (Controller / Configarator) Thread State Machine
```mermaid
    stateDiagram-v2
    [*] --> InitNCurses: Initialize ncurses and colors
    InitNCurses --> NodeSelectionPhase
    
    state NodeSelectionPhase {
        [*] --> ShowNodeMenu: Display node selection menu
        ShowNodeMenu --> WaitNodeInput: Wait for user input
        WaitNodeInput --> HandleKeyUp: KEY_UP
        WaitNodeInput --> HandleKeyDown: KEY_DOWN
        WaitNodeInput --> HandleEnter: ENTER key
        
        HandleKeyUp --> UpdateNodeHighlight: Move highlight up
        HandleKeyDown --> UpdateNodeHighlight: Move highlight down
        UpdateNodeHighlight --> ShowNodeMenu: Redraw menu
        
        HandleEnter --> ValidateNodeSelection: Check if valid node
        ValidateNodeSelection --> ShowNodeError: Invalid node
        ValidateNodeSelection --> NodeSelected: Valid node (NODE_TYPE_1 or NODE_TYPE_2)
        
        ShowNodeError --> WaitErrorInput: Display error and wait
        WaitErrorInput --> ShowNodeMenu: Return to menu
        
        NodeSelected --> [*]: Exit node selection
    }
    
    NodeSelectionPhase --> SetDynamicMenu: Configure menu based on selected node
    SetDynamicMenu --> NotifyUARTThread: Lock mutex, set shared_node_type, unlock
    NotifyUARTThread --> CommandMenuPhase
    
    state CommandMenuPhase {
        [*] --> ShowCommandMenu: Display command menu with system info
        ShowCommandMenu --> ReadStatusFromUART: Lock mutex, read status_response/receive_data/status_color, unlock
        ReadStatusFromUART --> DisplayStatus: Show status and received data
        DisplayStatus --> WaitCommandInput: Wait for user input
        
        WaitCommandInput --> HandleCmdUp: KEY_UP
        WaitCommandInput --> HandleCmdDown: KEY_DOWN  
        WaitCommandInput --> HandleCmdEnter: ENTER key
        
        HandleCmdUp --> UpdateCmdHighlight: Move highlight up
        HandleCmdDown --> UpdateCmdHighlight: Move highlight down
        UpdateCmdHighlight --> ShowCommandMenu: Redraw menu
        
        HandleCmdEnter --> CheckBusyStatus: Read is_busy flag
        CheckBusyStatus --> ShowBusyMessage: System is busy
        CheckBusyStatus --> SendCommandToUART: System ready
        
        ShowBusyMessage --> ShowCommandMenu: Continue menu loop
        
        SendCommandToUART --> LockCommandMutex: Lock command_mutex
        LockCommandMutex --> SetCommandCode: command_code = highlight + 1
        SetCommandCode --> SetCommandPending: command_pending = 1
        SetCommandPending --> UnlockCommandMutex: Unlock mutex
        UnlockCommandMutex --> CheckExitCondition: Check if exit command
        
        CheckExitCondition --> ExitProgram: Exit command for current node
        CheckExitCondition --> ShowCommandMenu: Continue menu loop
        
        ExitProgram --> CleanupUI: endwin(), close(uart_fd)
        CleanupUI --> [*]: pthread_exit(NULL)
    }
    
    CommandMenuPhase --> [*]: UI Thread terminated
```

#### Bản đơn giản:
```mermaid
stateDiagram-v2
    [*] --> InitUI: Setup ncurses
    InitUI --> SelectNode: Choose node type
    SelectNode --> ShowMenu: Display command menu
    ShowMenu --> HandleInput: Process user input
    HandleInput --> SendCommand: Send to UART thread
    HandleInput --> ShowMenu: Navigate menu
    SendCommand --> ShowMenu: Continue
    SendCommand --> Exit: Exit command
    Exit --> [*]: Close UI
```

### Communication Thread State Machine
```mermaid
stateDiagram-v2
    [*] --> MainLoop: Enter infinite processing loop
    MainLoop --> LockMutexRead: Lock command_mutex
    LockMutexRead --> ReadSharedVariables: Read cmd, pending, local_node_type
    ReadSharedVariables --> ClearPending: command_pending = 0 (mark as handled)
    ClearPending --> UnlockMutexRead: Unlock mutex
    
    UnlockMutexRead --> CheckNodeTypeSet: local_node_type == 0?
    CheckNodeTypeSet --> WaitForNodeSelection: No node selected yet
    CheckNodeTypeSet --> CheckCommandPending: Node already selected
    
    WaitForNodeSelection --> Sleep100ms: usleep(100 * 1000)
    Sleep100ms --> MainLoop: Continue waiting for UI thread to set shared_node_type
    
    CheckCommandPending --> WaitForCommand: No pending command
    CheckCommandPending --> ProcessCommand: Command is pending
    
    WaitForCommand --> Sleep10ms: usleep(10 * 1000)
    Sleep10ms --> MainLoop: Continue waiting for UI thread to send command
    
    ProcessCommand --> CheckBlockUI: Check if cmd in {1,2,3,6}
    CheckBlockUI --> SetBusyFlag: Set is_busy = 1 for blocking commands
    CheckBlockUI --> ExecuteCommandByNode: Execute based on local_node_type
    SetBusyFlag --> ExecuteCommandByNode
    
    state ExecuteCommandByNode {
        [*] --> CheckNodeType: Check local_node_type value
        CheckNodeType --> ExecuteNode1Commands: NODE_TYPE_1
        CheckNodeType --> ExecuteNode2Commands: NODE_TYPE_2
        
        state ExecuteNode1Commands {
            [*] --> SwitchNode1Cmd: Switch on command code
            SwitchNode1Cmd --> Direction1: case 1 - Direction1
            SwitchNode1Cmd --> Direction2: case 2 - Direction2  
            SwitchNode1Cmd --> Direction3: case 3 - Direction3
            SwitchNode1Cmd --> LedOn: case 4 - Led_On
            SwitchNode1Cmd --> LedOff: case 5 - Led_Off
            SwitchNode1Cmd --> SendStatus: case 6 - Send_Status
            SwitchNode1Cmd --> StopSystem: case 7 - Stop_System
            SwitchNode1Cmd --> InitSystem: case 8 - Init
            SwitchNode1Cmd --> ReflashFirmware: case 9 - Re-flash firmware
            
            Direction1 --> WriteDir1: write_command(CMD_DIRECTION_1)
            WriteDir1 --> ReadResp1: Read_Response(10000, &resp_len)
            
            Direction2 --> WriteDir2: write_command(CMD_DIRECTION_2)
            WriteDir2 --> ReadResp2: Read_Response(5000, &resp_len)
            
            Direction3 --> WriteDir3: write_command(CMD_DIRECTION_3)
            WriteDir3 --> ReadResp3: Read_Response(10000, &resp_len)
            
            LedOn --> WriteLedOn: write_command(CMD_LED_ON)
            LedOff --> WriteLedOff: write_command(CMD_LED_OFF)
            
            SendStatus --> WriteStatus: write_command(CMD_SEND_STATUS)
            WriteStatus --> ReadStatusResp: Read_Response(100, &resp_len)
            ReadStatusResp --> ParseT1T2T3: Parse T1, T2, T3 values
            
            StopSystem --> WriteStop: write_command(CMD_STOP_SYSTEM)
            InitSystem --> WriteInit: write_init(115200)
            
            ReflashFirmware --> SetBusyForFlash: is_busy = 1
            SetBusyForFlash --> UpdateFlashStatus: Lock mutex, set "Flashing firmware..." status
            UpdateFlashStatus --> ExecuteEsptool: system(esptool command)
            ExecuteEsptool --> ClearBusyAfterFlash: is_busy = 0
            
            ReadResp1 --> [*]: Node1 command complete
            ReadResp2 --> [*]: Node1 command complete
            ReadResp3 --> [*]: Node1 command complete
            WriteLedOn --> [*]: Node1 command complete
            WriteLedOff --> [*]: Node1 command complete
            ParseT1T2T3 --> [*]: Node1 command complete
            WriteStop --> [*]: Node1 command complete
            WriteInit --> [*]: Node1 command complete
            ClearBusyAfterFlash --> [*]: Node1 command complete
        }
        
        state ExecuteNode2Commands {
            [*] --> SwitchNode2Cmd: Switch on command code
            SwitchNode2Cmd --> LedOn2: case 1 - LED On
            SwitchNode2Cmd --> LedOff2: case 2 - LED Off
            SwitchNode2Cmd --> ReadSingle: case 3 - Read Single
            SwitchNode2Cmd --> ReadContinuous: case 4 - Read Continuous
            
            LedOn2 --> WriteLedOn2: write_command(CMD2_LED_ON)
            LedOff2 --> WriteLedOff2: write_command(CMD2_LED_OFF)
            ReadSingle --> WriteReadSingle: write_command(CMD2_READ_SINGLE)
            WriteReadSingle --> ReadSingleResp: Read_Response(100, &resp_len)
            ReadContinuous --> WriteReadCont: write_command(CMD2_READ_CONTINUOUS)
            WriteReadCont --> ReadContResp: Read_Response(100, &resp_len)
            
            WriteLedOn2 --> [*]: Node2 command complete
            WriteLedOff2 --> [*]: Node2 command complete
            ReadSingleResp --> [*]: Node2 command complete
            ReadContResp --> [*]: Node2 command complete
        }
        
        ExecuteNode1Commands --> [*]: Command execution complete
        ExecuteNode2Commands --> [*]: Command execution complete
    }
    
    ExecuteCommandByNode --> UpdateStatusForUI: Lock mutex, update status_response/receive_data/status_color
    UpdateStatusForUI --> UnlockStatusMutex: Unlock mutex
    UnlockStatusMutex --> FreeResponseMemory: if(resp) free(resp)
    FreeResponseMemory --> CheckClearBusy: Check if block_ui was set
    CheckClearBusy --> ClearBusyFlag: is_busy = 0 for blocking commands
    CheckClearBusy --> CommandComplete: Non-blocking command complete
    ClearBusyFlag --> CommandComplete
    CommandComplete --> MainLoop: Return to main processing loop
```

#### Bản đơn giản:
```mermaid
stateDiagram-v2
    [*] --> WaitNode: Wait for node selection
    WaitNode --> WaitCommand: Wait for command
    WaitCommand --> ExecuteCommand: Process command
    ExecuteCommand --> UpdateStatus: Update UI status
    UpdateStatus --> WaitCommand: Continue loop
```

### MQTT Send to thingsboard Thread State Machine:
```mermaid
stateDiagram-v2
    [*] --> Initialize: Setup MQTT client
    Initialize --> Connect: Connect to ThingsBoard
    Connect --> WaitConnection: Wait for connection (5s timeout)
    
    WaitConnection --> Connected: Connection successful
    WaitConnection --> Failed: Connection timeout/failed
    
    Connected --> PublishLoop: Enter main loop
    
    state PublishLoop {
        [*] --> CheckTime: Check if 1 second elapsed
        CheckTime --> CollectAndPublish: Time to publish
        CheckTime --> Sleep: Not time yet
        
        CollectAndPublish --> GetData: Get Node1 & Node2 data
        GetData --> SendTelemetry: Publish to ThingsBoard
        SendTelemetry --> Sleep: Data sent
        
        Sleep --> CheckConnection: Sleep 100ms
        CheckConnection --> CheckTime: Still connected
        CheckConnection --> Reconnect: Connection lost
        
        Reconnect --> ReconnectAttempt: Try mosquitto_reconnect()
        ReconnectAttempt --> ReconnectSleep: Sleep 1000ms
        ReconnectSleep --> CheckTime: Continue loop
    }
    
    Failed --> Cleanup: Clean up resources
    Cleanup --> [*]: Thread exit
    
    note right of Connected
        Callbacks run asynchronously:
        - on_connect: Set mqtt_connected=1, publish attributes
        - on_disconnect: Set mqtt_connected=0
        - on_publish: Message sent confirmation
    end note
```
### Bản đơn giản:
```mermaid
stateDiagram-v2
    [*] --> Initialize: Setup MQTT client & callbacks
    Initialize --> Connect: Connect to ThingsBoard
    Connect --> WaitConnection: Wait for connection
    WaitConnection --> Connected: Connection successful
    WaitConnection --> Timeout: Connection timeout
    
    Connected --> PublishLoop: Enter main publishing loop
    PublishLoop --> CollectData: Get Node1 & Node2 data
    CollectData --> PublishTelemetry: Send to ThingsBoard
    PublishTelemetry --> CheckConnection: Verify connection status
    CheckConnection --> PublishLoop: Continue if connected
    CheckConnection --> Reconnect: Try reconnect if disconnected
    Reconnect --> PublishLoop: Return to main loop
    
    Timeout --> Cleanup: Clean up resources
    Cleanup --> [*]: Thread exit
```
