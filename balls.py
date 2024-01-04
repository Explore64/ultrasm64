import os
import re
import threading
import sys
import getopt

def convert_tris(image_path, vert_buffer_size):
    file = open(image_path, mode = 'r')
    print("Opening " + image_path + ".")
    lines = file.readlines()
    file.close()
    triIndices = []
    vertCount = 0
    addrMerge = []
    vertAddr = ""
    lineNum = 0
    linesToDestroy = []
    vBuff = -1
    prevVBuff = 0
    write = False
    addrFound = False
    # Read all the lines in the file
    for line in lines:
        lineS = line.strip()
        if line.find("gsSPVertex") != -1: # Found a vertex load call.
            matches = re.findall(r'\d+', lineS)
            # Note how many verts are loaded.
            count = int(matches[len(matches) - 2])
            vertCount += count
            if (prevVBuff != 0):
                vBuff += prevVBuff
            prevVBuff = count
            matches2 = re.findall(r'\S+', lineS)
            # Save the name. If no first name exists, use that, otherwise, save it as a secondary list name.
            vertName = matches2[0][11:]
            #print(vertName)
            if (vertName[-1:] == ','):
                vertName = vertName[:-1]
            if (addrFound == False):
                addrFound = True
                vertAddr = vertName
                #print(vertAddr)
            elif vertName != vertAddr:
                addrMerge.append(vertName)
            if (lines[lineNum + 1].find("Triangle")) != -1:
                linesToDestroy.append(lineNum)
            #print(vBuff)
            #print("Prev: " + str(prevVBuff))
        elif (line.find("gsSP2Triangles")) != -1 or (line.find("gsSP1Triangle")) != -1: # Found a triangle command
            matches = re.findall(r'\d+', lineS)
            # Save all the triangle mesh indices into a list. Make sure to add the vertex total too, to prevent the numbers being useless.
            triIndices.append(int(matches[1]) + vBuff)
            triIndices.append(int(matches[2]) + vBuff)
            triIndices.append(int(matches[3]) + vBuff)
            if (int(matches[0]) == 2): # The first index is either 1 or 2, representing 1tri or 2tri
                triIndices.append(int(matches[6]) + vBuff)
                triIndices.append(int(matches[7]) + vBuff)
                triIndices.append(int(matches[8]) + vBuff)
            if (addrFound == True):
                linesToDestroy.append(lineNum)
            #print(matches)
        else:
            if (vertCount > 0) and (len(triIndices) > 0):
                #print("Indices: " + str(triIndices))
                #print("Total Triangles: " + str(int(len(triIndices) / 3)))
                #print("Total Vertices: " + str(vertCount))

                vertsIn = 0
                newLine = ""
                curIndices = []
                minVert = 999
                maxVert = 0
                vertOffset = 0
                write = True

                while (len(triIndices) > 0 or len(curIndices) > 0):
                    if (len(curIndices) == 0):
                        while (True):
                            if (len(triIndices) > 0 and triIndices[0] - vertsIn < vert_buffer_size + vertOffset and triIndices[1] - vertsIn < vert_buffer_size + vertOffset and triIndices[2] - vertsIn < vert_buffer_size + vertOffset):
                                curIndices.append(triIndices[0])
                                curIndices.append(triIndices[1])
                                curIndices.append(triIndices[2])
                                minVert = min(triIndices[0], triIndices[1], triIndices[2], minVert)
                                if (minVert - vertsIn < 0):
                                    vertOffset = minVert - vertsIn
                                del triIndices[:3]
                            else:
                                #print(minVert - vertsIn)
                                maxVert = min(vert_buffer_size, vertCount - vertsIn)
                                vertsIn += vertOffset
                                newLine += "    gsSPVertex(" + str(vertAddr) + " + " + str(vertsIn) + ", " + str(maxVert) + ", 0),\n"
                                vertsIn += maxVert
                                vertOffset = 0
                                #print("Loaded " + str(maxVert) + " vertices.")
                                #print("Tris to draw: " + str(int(len(curIndices) / 3)))
                                minVert = 999
                                break
                    else:
                        offset = vertsIn - maxVert
                        if (len(curIndices) > 3):
                            newLine += "    gsSP2Triangles(" + str(curIndices[0] - offset) + ", " + str(curIndices[1] - offset) + ", " + str(curIndices[2] - offset) + ", 0x0, " + str(curIndices[3] - offset) + ", " + str(curIndices[4] - offset) + ", " + str(curIndices[5] - offset) + ", 0x0),\n"
                            del curIndices[:6]
                        else:
                            newLine += "    gsSP1Triangle(" + str(curIndices[0] - offset) + ", " + str(curIndices[1] - offset) + ", " + str(curIndices[2] - offset) + ", 0x0),\n"
                            del curIndices[:3]
                    #print("Current Tris remaining: " + str(int(len(curIndices) / 3)))
                    #print("Total Tris remaining: " + str(int(len(triIndices) / 3)))

                #print(newLine)
                while (len(linesToDestroy) > 0):
                    lines[linesToDestroy[0]] = ""
                    del linesToDestroy[:1]
                lines.insert(lineNum, newLine)
                
                # Find and merge necessary vertex lists.
                newVertLine = ""
                if (len(addrMerge)) != 0:
                    #print(addrMerge)
                    baseFound = False
                    vertLine = 0
                    addrsNuked = 0
                    baseLine = 0
                    vertLineSnap = False
                    vertBufferLinesToDestroy = []
                    for vLine in lines:
                        if (len(addrMerge) <= addrsNuked):
                            break
                        vLineS = vLine.strip()
                        #print(vLineS)
                        if (vLineS.find(vertAddr) != -1 and baseFound == False):
                            baseFound = True
                        elif (baseFound == True and vLineS.find("};") != -1 and baseLine == 0):
                            baseLine = vertLine
                        elif any(folder in vLine for folder in addrMerge) and vertLineSnap == False:
                            vertLineSnap = True
                            vertBufferLinesToDestroy.append(vertLine)
                        elif (vertLineSnap == True):
                            vertBufferLinesToDestroy.append(vertLine)
                            if vLineS.find("};") != -1:
                                vertBufferLinesToDestroy.append(vertLine + 1)
                                if (lines[vertLine + 1] == '\n'):
                                    vertBufferLinesToDestroy.append(vertLine + 2)
                                vertLineSnap = False
                                addrsNuked += 1
                            else:
                                newVertLine += vLine
                        vertLine += 1
                    lines.insert(baseLine, newVertLine)
                    #print(newVertLine)
                    addrMerge.clear()
                    numLines = len(vertBufferLinesToDestroy)
                    while (len(vertBufferLinesToDestroy) > 0):
                        lines[vertBufferLinesToDestroy[0]] = ""
                        del vertBufferLinesToDestroy[:1]
            vertCount = 0
            vBuff = 0
            prevVBuff = 0
            triIndices.clear()
            addrMerge.clear()
            addrFound = False
        lineNum += 1
    if (write == True):
        newFile = open(image_path, mode = 'w')
        newFile.writelines(lines)
        newFile.close()
        print("Processed " + image_path + ".")

def job(vert_buffer_size):
    #folder_path = "./levels/wmotr/areas/1"
    folder_path = "./actors/mario"
    folder_whitelist = ["./"]
    folder_blacklist = ["anims", "geo", "script", "collision", "texture"]
    for root, dirs, files in os.walk(folder_path):
        if any(folder in root for folder in folder_whitelist):
            for filename in files:
                if any(folder2 in root for folder2 in folder_blacklist) or any(folder2 in filename for folder2 in folder_blacklist):
                    continue
                if (filename.endswith(".c")):
                    image_path = os.path.join(root, filename)
                    tempThread = threading.Thread(target=convert_tris, args=(image_path, vert_buffer_size))
                    tempThread.start()


def main(argv):
    vert_buffer_size = 32
    opts, args = getopt.getopt(argv,"v:")
    for opt, arg in opts:
        if opt == '-v':
            vert_buffer_size = arg
    ucodeString = ""
    if (int(vert_buffer_size) > 56):
        print("ERROR: Vertex Buffer size too high.")
        sys.exit()
    elif (int(vert_buffer_size) > 32):
        ucodeString = "F3DEX3"
    elif (int(vert_buffer_size) > 16):
        ucodeString = "F3DEX or F3DEX2"
    else:
        ucodeString = "F3D, F3DEX, F3DEX2 or F3DEX3"
    print("Running Vertex merge!\nVertex Buffer size: " + str(vert_buffer_size) + ".\nWorks with " + ucodeString + ".")
    job(int(vert_buffer_size))

if __name__ == "__main__":
   main(sys.argv[1:])