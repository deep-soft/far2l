/*
  PDF.CPP

  Second-level plugin module for FAR Manager and MultiArc plugin

  Copyright (c) 2017 Andrey Bachmaga lordakryl@gmail.com
*/

#include <windows.h>
#include <utils.h>
#include <farplug-mb.h>
#include <sstream>
#include <iostream>
#include <string>

#include "./PDF-Writer-master/PDFWriter/PDFParser.h"
#include "./PDF-Writer-master/PDFWriter/InputFile.h"
#include "./PDF-Writer-master/PDFWriter/PDFObject.h"
#include "./PDF-Writer-master/PDFWriter/PDFDictionary.h"
#include "./PDF-Writer-master/PDFWriter/PDFObjectCast.h"
#include "./PDF-Writer-master/PDFWriter/PDFIndirectObjectReference.h"
#include "./PDF-Writer-master/PDFWriter/PDFArray.h"
#include "./PDF-Writer-master/PDFWriter/PDFDictionary.h"
#include "./PDF-Writer-master/PDFWriter/PDFStreamInput.h"

using namespace PDFHummus;
using namespace oldfar;
using namespace std;
#include "fmt.hpp"


#if defined(__BORLANDC__)
  #pragma option -a1
#elif defined(__GNUC__) || (defined(__WATCOMC__) && (__WATCOMC__ < 1100)) || defined(__LCC__)
  #pragma pack(1)
  #if defined(__LCC__)
    #define _export __declspec(dllexport)
  #endif
#else
  #pragma pack(push,1)
  #if _MSC_VER
    #define _export
  #endif
#endif


/*
#ifdef _MSC_VER
#if _MSC_VER < 1310
#pragma comment(linker, "/ignore:4078")
#pragma comment(linker, "/merge:.data=.")
#pragma comment(linker, "/merge:.rdata=.")
#pragma comment(linker, "/merge:.text=.")
#pragma comment(linker, "/section:.,RWE")
#endif
#endif
*/

void showPageContent(PDFParser&,RefCountPtr<PDFObject>,InputFile&);

class PDFTreeIterator {
    PDFObject *inObject;
public:
    PDFTreeIterator(PDFObject &obj, PDFParser &pdfParser)
    {
        inObject = &obj;
    }
    ~PDFTreeIterator()
    {

    }

    BOOL Next() {
        return FALSE;
    }

    string GetName() {
        return string(PDFObject::scPDFObjectTypeLabel(inObject->GetType()));
    }

    string GetPath(string PrevPath) {
        return PrevPath + "/" + GetName();
    }
};

class PDFTraverser
{
    InputFile pdfFile;
    PDFParser pdfParser;
    PDFDictionary* page;
    PDFObject* contents;
    MapIterator<PDFNameToPDFObjectMap> *it;
    BOOL valid;
    unsigned int CurPage;

private:
    string BuildPath(unsigned int pageNr, string objName)
    {
        return string("Page ")
            + to_string(pageNr+1)
            + string("_ID")
            + to_string(pdfParser.GetPageObjectID(pageNr))
            + string("/")
            + objName;
    }
public:
    PDFTraverser(const char *path) : valid(FALSE), page(NULL), it(NULL), contents(NULL), CurPage(0)
    {
        string file_name(path);
        valid = TRUE;

        if (pdfFile.OpenFile(file_name) != PDFHummus::eSuccess)
            valid = FALSE;
        if (valid && pdfParser.StartPDFParsing(pdfFile.GetInputStream()) != PDFHummus::eSuccess)
            valid = FALSE;
    }

    ~PDFTraverser() {
        if (NULL != contents) {
            delete contents;
        }
        if (NULL != page) {
            delete page;
        }
        if (NULL != it)
            delete it;
        valid = FALSE;
    }

    int Next(struct ArcItemInfo *Info)
    {
        if (!valid)
            return GETARC_READERROR;
        if (CurPage > pdfParser.GetPagesCount() - 1)
            return GETARC_EOF;

        if (NULL == page) {
            page = pdfParser.ParsePage(CurPage);
            /*
            contents = pdfParser.QueryDictionaryObject(page, "Contents"); */
            PDFObjectCastPtr<PDFDictionary> resources(pdfParser.QueryDictionaryObject(page, "Resources"));
            if (resources != NULL) {
                PDFObjectCastPtr<PDFDictionary> xobjects(pdfParser.QueryDictionaryObject(resources.GetPtr(),"XObject"));
                MapIterator<PDFNameToPDFObjectMap> itr = xobjects->GetIterator();
                if (itr.MoveNext())
                    itr.GetKey();
            } // */
        }


        bool nextPage = true;
        if (!contents) {
            nextPage = true;
            Info->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        } else {
            Info->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE;
            showPageContent(pdfParser, contents, pdfFile);
        }


        string sPath = BuildPath(CurPage, "ttt");
        Info->PathName.assign(sPath.c_str(), sizeof(Info->PathName));
//        strncpy(Info->cFileName, sPath.c_str(), sizeof(Info->cFileName));

        ostringstream os; os << pdfParser.GetPDFLevel();
// NOTOK        Info->HostOS.assign(os.str().c_str(), sizeof(Info->HostOS));
//        strncpy(Info->HostOS, os.str().c_str(), sizeof(Info->HostOS));

        Info->nFileSize=pdfParser.GetObjectsCount();
        Info->nPhysicalSize=Info->nFileSize;

        if (nextPage) {
            CurPage++;
            if(NULL != page) {
                delete page;
                page = NULL;
            }
            if(NULL != contents) {
                delete contents;
                contents = NULL;
            }
        }
        return GETARC_SUCCESS;
    }

    BOOL Valid()
    {
        return valid;
    }
};

///////////////////////////////////

static PDFTraverser *s_selected_traverser = NULL;

void showPagesInfo(PDFParser&,InputFile&,EStatusCode);
void checkXObjectRef(PDFParser&,RefCountPtr<PDFDictionary>);
void showXObjectsPerPageInfo(PDFParser&,PDFObjectCastPtr<PDFDictionary>);
//void showPageContent(PDFParser&,RefCountPtr<PDFObject>,InputFile&);
void showContentStream(PDFStreamInput*,IByteReaderWithPosition*,PDFParser&);

void WINAPI UnixTimeToFileTime( DWORD time, FILETIME * ft );

void  WINAPI _export PDF_SetFarInfo(const struct PluginStartupInfo *Info)
{
  ;
}

BOOL WINAPI _export PDF_IsArchive(const char *Name,const unsigned char *Data,int DataSize)
{
  if (DataSize<26 || Data[0]!='%' || Data[1]!='P' || Data[2]!='D' || Data[3]!='F' || Data[4]!='-')
    return(FALSE);
  return(TRUE);
}

BOOL WINAPI _export PDF_OpenArchive(const char *Name, int *Type, bool Silent)
{
    PDFTraverser *t = new PDFTraverser(Name);
    if (!t->Valid())
    {
        delete t;
        return (FALSE);
    }

    if (NULL != s_selected_traverser)
        delete s_selected_traverser;
    s_selected_traverser = t;
    return (TRUE);
}


//    showPagesInfo(parser,pdfFile,status);

void showPagesInfo(PDFParser& parser, InputFile& pdfFile, EStatusCode status)
{
    for(unsigned long i=0; i < parser.GetPagesCount() && eSuccess == status; ++i) {
        RefCountPtr<PDFDictionary> page(parser.ParsePage(i));
        checkXObjectRef(parser,page);
        RefCountPtr<PDFObject> contents(parser.QueryDictionaryObject(page.GetPtr(),"Contents"));
        if(!contents) {
                cout << "No contents for this page\n"; continue;
        }
        showPageContent(parser,contents,pdfFile);
    }
}

void checkXObjectRef(PDFParser& parser,RefCountPtr<PDFDictionary> page)
{
    PDFObjectCastPtr<PDFDictionary> resources(parser.QueryDictionaryObject(page.GetPtr(),"Resources"));
    if(!resources)
    {
        wcout << "No XObject in this page\n";
        return;
    }

    PDFObjectCastPtr<PDFDictionary> xobjects(parser.QueryDictionaryObject(resources.GetPtr(),"XObject"));
    if(!xobjects)
    {
        wcout << "No XObject in this page\n";
        return;
    }

    cout << "Displaying XObjects information for this page:\n";
    showXObjectsPerPageInfo(parser,xobjects);
}

void showXObjectsPerPageInfo(PDFParser& parser,PDFObjectCastPtr<PDFDictionary> xobjects)
{
    RefCountPtr<PDFName> key;
    PDFObjectCastPtr<PDFIndirectObjectReference> value;
    MapIterator<PDFNameToPDFObjectMap> it = xobjects->GetIterator();
    while(it.MoveNext())
    {
        key = it.GetKey();
        value = it.GetValue();

        cout << "XObject named " << key->GetValue().c_str() << " is object " << value->mObjectID << " of type ";

        PDFObjectCastPtr<PDFStreamInput> xobject(parser.ParseNewObject(value->mObjectID));
        PDFObjectCastPtr<PDFDictionary> xobjectDictionary(xobject->QueryStreamDictionary());
        PDFObjectCastPtr<PDFName> typeOfXObject = xobjectDictionary->QueryDirectObject("Subtype");

        cout << typeOfXObject->GetValue().c_str() << "\n";
    }
}

void showPageContent(PDFParser& parser, RefCountPtr<PDFObject> contents, InputFile& pdfFile)
{
    if(contents->GetType() == PDFObject::ePDFObjectArray)
    {
        PDFObjectCastPtr<PDFIndirectObjectReference> streamReferences;
        SingleValueContainerIterator<PDFObjectVector> itContents = ((PDFArray*)contents.GetPtr())->GetIterator();
        // array of streams
        while(itContents.MoveNext())
        {
            streamReferences = itContents.GetItem();
            PDFObjectCastPtr<PDFStreamInput> stream = parser.ParseNewObject(streamReferences->mObjectID);
            showContentStream(stream.GetPtr(),pdfFile.GetInputStream(),parser);
        }
    }
    else
    {
        // stream
        showContentStream((PDFStreamInput*)contents.GetPtr(),pdfFile.GetInputStream(),parser);
    }
}

void showContentStream(PDFStreamInput* inStream,IByteReaderWithPosition* inPDFStream,PDFParser& inParser)
{
    IByteReader* streamReader = inParser.CreateInputStreamReader(inStream);
    Byte buffer[1000];
    if(streamReader)
    {
        inPDFStream->SetPosition(inStream->GetStreamContentStart());
        while(streamReader->NotEnded())
        {
            LongBufferSizeType readAmount = streamReader->Read(buffer,1000);
            cout.write((const char*)buffer,readAmount);
        }
        cout << "\n";
    }
    else
        cout << "Unable to read content stream\n";
    delete streamReader;
}

int WINAPI _export PDF_GetArcItem(struct ArcItemInfo *Info)
{
    if (!s_selected_traverser)
        return GETARC_READERROR;
    return s_selected_traverser->Next(Info);
}

BOOL WINAPI _export PDF_CloseArchive(struct ArcInfo *Info)
{
    if (NULL != s_selected_traverser)
        delete s_selected_traverser;
    return(TRUE);
}

BOOL WINAPI _export PDF_GetFormatName(int Type,char *FormatName,char *DefaultExt)
{
  if (Type==0)
  {
    strcpy(FormatName,"PDF");
    strcpy(DefaultExt,"pdf");
    return(TRUE);
  }
  return(FALSE);
}


BOOL WINAPI _export PDF_GetDefaultCommands(int Type,int Command,char *Dest)
{
  if (Type==0)
  {
    static const char *Commands[]={
    /*Extract               */"hap xay %%a %%FMQ",
    /*Extract without paths */"hap eay %%a %%FMQ",
    /*Test                  */"hap t %%a %%FMQ",
    /*Delete                */"hap d %%a %%FMQ",
    /*Comment archive       */"",
    /*Comment files         */"",
    /*Convert to SFX        */"",
    /*Lock archive          */"",
    /*Protect archive       */"",
    /*Recover archive       */"",
    /*Add files             */"hap as2{%%S} %%a %%FQ",
    /*Move files            */"hap asm2{%%S} %%a %%FQ",
    /*Add files and folders */"hap asr2{%%S} %%a %%FMQ",
    /*Move files and folders*/"hap asmr2{%%S} %%a %%FMQ",
    /*"All files" mask      */"*.*"
    };
    if (Command<(int)(ARRAYSIZE(Commands)))
    {
      strcpy(Dest,Commands[Command]);
      return(TRUE);
    }
  }
  return(FALSE);
}
