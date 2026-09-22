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


22 september - Andre law
1hr


----
## Moses Man - 28 Minutes
Merging custom repo 'Vital' into 'VitalQuant' Repo - it contains the new source code
