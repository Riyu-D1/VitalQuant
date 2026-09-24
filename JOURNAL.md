## 21st September - Riyansh D
45mins
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
external optical emitters/detector where required by the MAX architecture

#Temperature
MLX90637 or MLX90632 — needs a deliberate comparison before locking it in

#Environmental
BME280 — temperature/humidity/pressure
BME680 only if we establish a reason for the gas channel

#Contact
FSR sensing element + simple PCB interface


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
Merging custom repo 'Vital' into 'VitalQuant' Repo - it contains the new source code

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
