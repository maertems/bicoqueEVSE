
//----------------
//
//
// Functions
//
//
//----------------
// ********************************************
// SPIFFFS storage Functions
// ********************************************


String storageDir(String name)
{
    String output;
    Dir dir = SPIFFS.openDir(name);
    while (dir.next())
    {
      output += dir.fileName();
      output += " - ";
      File f = dir.openFile("r");
      output += f.size();
      output += "\n";
    }

   return output;
}
String storageRead(String fileName)
{
  String dataText;

  File file = SPIFFS.open(fileName, "r");
  if (!file)
  {
    //logger("FS: opening file error");
    //-- debug
  }
  else
  {
    size_t sizeFile = file.size();
    if (sizeFile > 1000 )
    {
       Serial.println("Size of file is too clarge");
    }
    else
    {
      dataText = file.readString();
    }
  }

  file.close();
  return dataText;
}

void storageDel(String fileName)
{
  SPIFFS.remove(fileName);
}

bool storageWrite(char *fileName, String dataText)
{
  File file = SPIFFS.open(fileName, "w");
  file.println(dataText);

  file.close();

  return true;
}

bool storageAppend(String fileName, String dataText)
{
  File file = SPIFFS.open(fileName, "a");
  file.print(dataText);
  file.close();
  return true;
}

bool storageClear(char *fileName)
{
  SPIFFS.remove(fileName);
  return true;
}

