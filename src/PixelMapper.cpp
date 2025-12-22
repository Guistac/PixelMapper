#include "PixelMapper.h"

//called on any fixture addition, removal or size change
void Patch::buildPixels(){
    //count the new pixel amount
    int pixelCount = 0;
    for(auto& fixture : fixtures) pixelCount += fixture.pixelCount;

    //clear the pixel list and rebuild it
    pixels.clear();
    pixels.resize(pixelCount);

    //each fixture gets a reference to its first pixel in the pixel list
    //compute each pixel position
    int pixelOffset = 0;
    for(auto& fixture : fixtures){
        fixture.startPixel = &pixels[pixelOffset];
        pixelOffset += fixture.pixelCount;
        fixture.calcPixelPositions();
    }

}


//compute the position of each pixel of the fixture
void Fixture::calcPixelPositions(){
    if(pixelCount <= 0) return;

    //linear interpolation between start and endpoint
    glm::vec2 size = endPos - startPos;
    for(int i = 0; i < pixelCount; i++){
        float range = float(i) / float(pixelCount - 1);
        startPixel[i].pos = startPos + range * size;
    }
}