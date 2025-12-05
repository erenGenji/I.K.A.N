# IKAN Project

Embedded IoT with **NO WIFI** needed. Equipped with Embedded Kendryte-based NPU for AI. **No expensive SOC** (Raspberry Pi, Jetson, mini computer). Built for fish farmers.

## Operating Modes

| Stand-Alone Mode  | Feeder Mode |
| --- | --- |
| AI-based water condition monitoring | AI-assisted automated feeding |

## PCB Documents

this repo was made with Kicad in mind. PLEASE MAKE A NEW BRANCH! DO NOT USE MAIN UNLESS ALLOWED.

## Documentation

please add all datasheets used here
as well as BOM file - PNP files - GERBER in respective files, if it doesnt exist do make one.

## WEBAPP

as of now i have no clue how web programming work this section will be leaved to akina

## AI Models used for Stand-Alone Mode


stateDiagram-v2
    Bubble_Detection_Model --> Algae_Bloom_Detection_Model
    Algae_Bloom_Detection_Model --> Bubble_Detection_Model

stateDiagram-v2
    FishSize_model --> FishFeed_Tracking_model
    FishFeed_Tracking_model --> FishMortality_model
    FishMortality_model --> FishFeed_Tracking_model
    FishMortality_model --> FishSize_model
