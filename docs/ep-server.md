# Eyeotine Protocol Server

The EP server is the software component on the server, that communicates with the ESPs.
It represents the server-side implementation of the EP.

## Data collection

The EP server needs to collect all the sensor data from the ESPs, bundle them and publish them, such that other trackers can retrieve their necessary input data.
For this, the following algorithm is used:

- Initialize a bucket marked by a timestamp
- Collect the incoming images in the bucket, while remembering, what image belongs to what ESP
- Timestamps with a variance of +-20ms should be put into the same bucket
- If a timestamp with greater difference arrives or the images from all sensors got updated, publish the current bucket

A bucket always contains an image for all ESPs.
When image data gets lost, the server should keep substitute the missing data with the image from the previous timestamp.


