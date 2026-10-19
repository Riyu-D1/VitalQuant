## 21st September - Riyansh D
1hr 30mins
##Initial product research on how to minimise space for sensors.

- the main issue was that the breakout boards for sensors take up to much space therefore to reduce the space used I decided to go for the ic versions of sensors
- This meant that I had to research each sensor I would be required to use and then its relevant ic component ensuring it does the same function in a smaller body without compromising on any data.
- Also I had to research and find which sellers near or around me would be able to sell theses components wether they were in wholesale or selling to hobbyists like me who only need a few rather than bulk
- lastly I just had a rough look at how the sensor structure would work as per the chosen sensors.
- This will all allow me to create a relatively small pcb and then ensure that the processing layer is all stacked on top of the sensing modality or on the other side of the board. 

Therefore the sensors I decide on for now are: 
#Optical / physiological
MAX86141 — HR/SpO₂ optical front end
AS7341-DLGM — spectral sensing
external optical emitters/detector where required by the MAX 
#Temperature
MLX90637 or MLX90632 
#Environmental
BME280 — temperature/humidity/pressure
BME680 only if we establish a reason for the gas channel
#Contact
FSR sensing element + simple PCB interface

*The lapse is a bit shorter than time I claimed to have worked as The recording stopped midway for some reason
<img width="1869" height="966" alt="image" src="https://github.com/user-attachments/assets/834e4db5-e02a-40cd-8007-7f738f2f47c2" />


## 22 september - Andre law
45 mins
## First variation of BOM

- The list of the first chosen sensors was looked at then researched as to wether they were the best chosen for their purpose or not
- After their functionality was confirmed I researched providers who were either local or had low shipping costs without compromising on time taken for delivery
- Then to narrow it down even further the shipping time and the overall product cost was added up get a rough idea of what providers are the best to choose.
- There is still lots of work which needs to be done to refine the spreadsheet and also the sheet will be updated many times according to the parts chosen or updated or even providers not being able to provide certain components in time or at a reasonable price therefore this will be updated frequently throughout the designing process and before the project is submitted for review.
<img width="1869" height="882" alt="image" src="https://github.com/user-attachments/assets/e85bae47-91ed-456c-aaf8-fe6c9860192c" />
 


----
## Moses Man - 28 Minutes
Merging custom repo 'Vital' into 'VitalQuant' Repo - it contains the new source code of the software in VitalQuant
<img width="958" height="470" alt="image" src="https://github.com/user-attachments/assets/3476ca2c-5cf6-4894-8291-82da5a963909" />
<img width="338" height="466" alt="image" src="https://github.com/user-attachments/assets/6a4b1a0e-3247-440c-a54f-595e50bf8b36" />



---
## 24 September - Riyansh D
30 mins

- I had a short break in school in which I researched what sensors are best to use for the charging component as even the esp32 we will be using will be a circuit module therefore it will have no ports.
- The best way I found was using usb c and rechargable lipo batteries
- The components I chose to were:
  - MCP73831 : I went with this for the linear charge management controller as it a very small integrated circuit which is really easy to use.
  - XC6206P332MR: This LDO was chosen as it uses very low standby power whilst being extremely efficient.
  - TPS7A2: Addtionally this LDO was chosen aswell as it takes up very little space and is a 200-mA ultra-low quiescent
  - CP2102: finally this was chosen as esp32 modules lack built in usb programming hardware therefore a USB to UART bridge IC was required.
- Now I need to move on to creating the initial pcb schematic and finding footprints for all of these components so they can be wired and visually seen in the eventual CAD file for the case development.
<img width="1869" height="966" alt="image" src="https://github.com/user-attachments/assets/c0abe951-9192-40eb-93fe-f0d6ebf889d0" />

----
## Moses - 26 Sept
3 hours
- Worked on the software and refactored the codebase to make it easier to debug issues when connecting with the software
- Installed and emulated a ESP32 to test firmware connectivity with our custom software
- Simulated test signals using the emulated ESP32 to test calibration of the software
<img width="931" height="449" alt="image" src="https://github.com/user-attachments/assets/351650c1-49eb-4521-ba3d-cbcd4cf8a326" />
<img width="698" height="461" alt="image" src="https://github.com/user-attachments/assets/4845104f-2735-4731-bc92-7fdba0c06c26" />

----
## Moses - 30 Sept
3 hours
- Downloading datasets of sepsis patient data
- Manipulating datasets ready for training
- Post-trained a proprietary custom decision model (LAT-D-VQt-1) via LoRA FP16 using LatticeAG infrastructure - 8x A100 80gb GPUs
- https://latticeag.vercel.app/#/products/lat-d-vqt-1
<img width="959" height="473" alt="image" src="https://github.com/user-attachments/assets/2a0ed26a-c70f-42ab-9dc2-edc92d3a1584" />
<img width="959" height="479" alt="image" src="https://github.com/user-attachments/assets/82be96a9-5820-4802-967b-1c7a29a431c7" />


---
## Riyansh - 30 sept
3 hrs 

- I spent time researching more components and I have now organised the pcb back side.
- I had recorded a lapse but this time was not recorded and there was some glitch where the lapse cannot be found (I have followed this up with hackclub)
- This took a lot of time as I had to ensure the research I had done was up to date with all the sensor research I had done.
- The new sensors were now organised only on the pcb and basic wiring was completed but this still needs to be heavily double checked and routing still has to be completed so lots of work will need to be put in to complete the pcb still.
- The BOM is all listed in the devin hw2_pcb branch
- This is just a very basic and initial render of the pcb
<img width="274" height="498" alt="image" src="https://github.com/user-attachments/assets/0db3fd01-83a3-4335-b2a0-09a58c8ec203" />


## Riyansh - 2 Oct
1hr 30 mins

- Between and school and other work I had to get some more time to basically fniallise what componanats were going to be used because this was just the intial desgin the one i had done on 30 sept.
- Turns out that there were many more upgrades that could be carried out which I had no idea about.
- A few listed ones being an onboard ecg and multiple other photo diodes
- I have listed the updated list in the main branch BOM list
- Andre should be creating a new updated list in the excel sheet with the new 80 parts

## Riyansh - 7 OCT
1hr 27 mins

- I had free day yesterday which I spent on more pcb research and completetion
- I spent 2 hrs the day before attempting to route the pcb which i forgot to log but this essentially was taking way to long and by the end was to complex and pretty much impossible for me to do as an individual
- As a result I gave the routing to devin and connected freerouter but devin also kept getting stuck on a churn and rip up loop.
- Therefore i did research and found a physics based simulation tool called quilter therefore I organised all the files and setup a job in quilter which will take anywhere from 4-24 hours to complete
- Therefore I will let this run overnight and checkup on it tomorrow
  
<img width="978" height="1478" alt="image" src="https://github.com/user-attachments/assets/59edd74b-1eb2-4d3d-9360-cc06a918c412" />

## Riyansh - 8 OCT
30 mins

- The quilter processes had finished so I rechecked over them and suprisiglly it was able to route only 92% as there were constriiction
- I resubmitted 2 jobs this time the old one to do another pass aswell as resubmitting a larger board which i will wait on the output.
- This should be able to now finish of the placement and routing
*Lapse cutoff and lost
<img width="1680" height="897" alt="image" src="https://github.com/user-attachments/assets/ebb42624-0448-4db9-af0e-cde097524b50" />

## Riyansh - 9 OCT
1hr 5mins
- So the 2 jobs ran overnight and there were basicallly very little changes
- The one that had been resubmitted had only 1 extra route and the one which was new managed to do 4 more making the total unrouted connections 45
- Therefore I realised that this routing was going to be practically impossible so i did some brainstorming and I came up with an idea to run my devin in cli and produce a pcb by connecting a kicad mcp
- The run just got stuck in a loop aswell so after more research I found the issue was that these machine learning algorithms use predicative programs and run many iterations as they cant actually see and understand whats on the pcb.
- To solve this i tried looking for other existiing skills butt there were none so I had to think of something myself
- I created a system using grok bot and devin. Because grok bot is agentic ai it can take pictures and has access to my computer and does not need my intervention therefore I told it to thoroughly read through the project repo and understand the aim. I set i up with the existiing quilter pcb and told it to take pictures and basicallly decode what the issue is and send over the details to devin.
- So far this has worked well and im going to let it run overnight
<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/1b30b9ed-f4ab-415a-abbd-fa0819557b68" />

## Riyansh - 10 OCTt
1hr 13mins
- So the grok bot ran overnight and this has absolutely amazing results the unnconectted were now down from 45 to 5
- I was very surprised that this was even the case but we somehow managed to get through it.
- Now to diagnose the last 5  opened the file ini kicad and ran DRC
- turns out that The issue was the sensor island because it was very narrow and cutoff from the rest of the board so it was basically impossible to reconnect
- the reseason I had included the island ini the first place because most biotech wearables have it for better heat dissipation from high voltage componants.
- I realised that for this board that wasnt a issue and did not affectt funcitonality so i removed the island and got grok to attempt to finish of routing
- Devin ran the program and managed to not only route the full board but also make it fab ready by rechecking errors and simulating a sepsis detection test using our existing early detection python script.
- There were only a few issues concerninig the 3v3 rail which had to be moved to 5v to supply more volts for the leds
<img width="2030" height="1510" alt="image" src="https://github.com/user-attachments/assets/db3e7733-fa1d-40b0-b3c6-51d726cfff1a" />

## Riyansh - 11 OCT
30 mins
- So now that the board/pcb was completely ready I ran simulations on it and the hardware performed as expected.  
- There wasnt much to do today and I was busy with school work so i started with the initial case design
- the current design will host the pcb along with the lipo and then openings for each of the contact and skin facing sensors.
- So i just quickly mapped out the rough area and will carry on the rest when i get some free time.
<img width="1600" height="1600" alt="image" src="https://github.com/user-attachments/assets/dc6efab5-cfd8-446e-906a-4141e5011af6" />

## Riyansh - 12 OCT
1hr
- I had much more free time today so I spent that time refining the case
- the main issue was that previously the pcb wasn't fully developed so i was still unsure on sensor placement but now that all the componants were locked i could properly mount the pcb in the case and start building around it
- So now the main part of the case which had to be developed was the skin facing siide as thats where the optical and touch sensors siit therefore they need exact placement unlike the rest.
- I traced and initially sketched out a rough idea then transfered it to CAD along with a step file of the pcb
*There is some glitch with the screen recording so I have started recording myself using my phone as that works
<img width="512" height="646" alt="image" src="https://github.com/user-attachments/assets/52189d33-4608-4ee8-ae92-195a8d064884" />

## Riyansh - 13 OCT
1hr
- Now all that was left was to complete the rest of the case
- So to do that II wanted to make a easy fit desgin for the first prototype as this would be easier to test and work with
- I do have a heat set inserts but those are not required for a first prototype case and can be integrated into the next one because I will have to open and close the case many times
- So All the was left was to create a snap design for the top piece so it could fit with the bottom piece i designed yesterday.
- Then there was also the battery compartment which I left as a seperate and removable peice on the top of the case so that it could be removed and fit easily this also allows for better waterproofing.
- The water proofiing is just a concept but I have left grooves for like a simple tpu gasket.
Exploded render veiw in a graph style interface for size perception:
<img width="573" height="528" alt="image" src="https://github.com/user-attachments/assets/0dd6ed9a-98a1-4433-a7c1-1db4d1d87df9" />




