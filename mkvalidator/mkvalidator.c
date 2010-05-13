/*
 * $Id$
 * Copyright (c) 2010, Matroska Foundation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Matroska Foundation nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY The Matroska Foundation ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL The Matroska Foundation BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "mkvalidator_stdafx.h"
#include "mkvalidator_project.h"
#include "matroska/matroska.h"

/*!
 * \todo validate the SeekHead position vs the element found
 * \todo validate that the Cues entries link to a proper block that is a keyframe
 * \todo optionally test that the Cluster's first video track is a keyframe
 * \todo optionally test that the Codecs match a certain combination
 * \todo optionally show the use of deprecated elements
 */

static textwriter *StdErr = NULL;

#ifdef TARGET_WIN
#include <windows.h>
void DebugMessage(const tchar_t* Msg,...)
{
#if !defined(NDEBUG) || defined(LOGFILE) || defined(LOGTIME)
	va_list Args;
	tchar_t Buffer[1024],*s=Buffer;

	va_start(Args,Msg);
	vstprintf_s(Buffer,TSIZEOF(Buffer), Msg, Args);
	va_end(Args);
	tcscat_s(Buffer,TSIZEOF(Buffer),T("\r\n"));
#endif

#ifdef LOGTIME
    {
        tchar_t timed[1024];
        SysTickToString(timed,TSIZEOF(timed),GetTimeTick(),1,1,0);
        stcatprintf_s(timed,TSIZEOF(timed),T(" %s"),s);
        s = timed;
    }
#endif

#if !defined(NDEBUG)
	OutputDebugString(s);
#endif

#if defined(LOGFILE)
{
    static FILE* f=NULL;
    static char s8[1024];
    size_t i;
    if (!f)
#if defined(TARGET_WINCE)
    {
        tchar_t DocPath[MAXPATH];
        char LogPath[MAXPATH];
        charconv *ToStr = CharConvOpen(NULL,CHARSET_DEFAULT);
        GetDocumentPath(NULL,DocPath,TSIZEOF(DocPath),FTYPE_LOG); // more visible via ActiveSync
        if (!DocPath[0])
            tcscpy_s(DocPath,TSIZEOF(DocPath),T("\\My Documents"));
        if (!PathIsFolder(NULL,DocPath))
            FolderCreate(NULL,DocPath);
        tcscat_s(DocPath,TSIZEOF(DocPath),T("\\corelog.txt"));
        CharConvST(ToStr,LogPath,sizeof(LogPath),DocPath);
        CharConvClose(ToStr);
        f=fopen(LogPath,"a+b");
        if (!f)
            f=fopen("\\corelog.txt","a+b");
    }
#else
        f=fopen("\\corelog.txt","a+b");
#endif
    for (i=0;s[i];++i)
        s8[i]=(char)s[i];
    s8[i]=0;
    fputs(s8,f);
    fflush(f);
}
#endif
}
#endif

static int OutputError(int ErrCode, const tchar_t *ErrString, ...)
{
	tchar_t Buffer[MAXLINE];
	va_list Args;
	va_start(Args,ErrString);
	vstprintf_s(Buffer,TSIZEOF(Buffer), ErrString, Args);
	va_end(Args);
	TextPrintf(StdErr,T("ERR%03X: %s!\r\n"),ErrCode,Buffer);
	return -ErrCode;
}

static void CheckUnknownElements(ebml_element *Elt)
{
	tchar_t IdStr[32], String[MAXPATH];
	ebml_element *SubElt;
	for (SubElt = EBML_MasterChildren(Elt); SubElt; SubElt = EBML_MasterNext(SubElt))
	{
		if (Node_IsPartOf(SubElt,EBML_DUMMY_ID))
		{
			Node_FromStr(Elt,String,TSIZEOF(String),Elt->Context->ElementName);
			EBML_IdToString(IdStr,TSIZEOF(IdStr),SubElt->Context->Id);
			OutputError(12,T("Unknown element in %s %s at %lld (size %lld)"),String,IdStr,(long)SubElt->ElementPosition,(long)SubElt->DataSize);
		}
		else if (Node_IsPartOf(SubElt,EBML_MASTER_CLASS))
		{
			CheckUnknownElements(SubElt);
		}
	}
}

static int CheckProfileViolation(ebml_element *Elt, int ProfileMask)
{
	int Result = 0;
	tchar_t String[MAXPATH],Invalid[MAXPATH];
	ebml_element *SubElt;
	const tchar_t *Profile[5] = {T("unknown"), T("v1"), T("v2"), T("und"), T("test") };

	Node_FromStr(Elt,String,TSIZEOF(String),Elt->Context->ElementName);
	if (Node_IsPartOf(Elt,EBML_MASTER_CLASS))
	{
		for (SubElt = EBML_MasterChildren(Elt); SubElt; SubElt = EBML_MasterNext(SubElt))
		{
			if (!Node_IsPartOf(SubElt,EBML_DUMMY_ID))
			{
				const ebml_semantic *i;
				for (i=Elt->Context->Semantic;i->eClass;++i)
				{
					if (i->eClass->Id==SubElt->Context->Id)
					{
						if ((i->DisabledProfile & ProfileMask)!=0)
						{
							Node_FromStr(Elt,Invalid,TSIZEOF(Invalid),i->eClass->ElementName);
							Result |= OutputError(0x201,T("Invalid %s for profile %s at %lld in %s"),Invalid,Profile[ProfileMask],(long)SubElt->ElementPosition,String);
						}
						break;
					}
				}
				if (Node_IsPartOf(SubElt,EBML_MASTER_CLASS))
					Result |= CheckProfileViolation(SubElt, ProfileMask);
			}
		}
	}

	return Result;
}

static int CheckMandatory(ebml_element *Elt, int ProfileMask)
{
	int Result = 0;
	tchar_t String[MAXPATH],Missing[MAXPATH];
	ebml_element *SubElt;

	Node_FromStr(Elt,String,TSIZEOF(String),Elt->Context->ElementName);
	if (Node_IsPartOf(Elt,EBML_MASTER_CLASS))
	{
		const ebml_semantic *i;
		for (i=Elt->Context->Semantic;i->eClass;++i)
		{
			if ((i->DisabledProfile & ProfileMask)==0 && i->Mandatory && !i->eClass->HasDefault && !EBML_MasterFindChild(Elt,i->eClass))
			{
				Node_FromStr(Elt,Missing,TSIZEOF(Missing),i->eClass->ElementName);
				Result |= OutputError(0x200,T("Missing element %s in %s at %lld"),Missing,String,(long)Elt->ElementPosition);
			}
		}

		for (SubElt = EBML_MasterChildren(Elt); SubElt; SubElt = EBML_MasterNext(SubElt))
		{
			if (Node_IsPartOf(SubElt,EBML_MASTER_CLASS))
				Result |= CheckMandatory(SubElt, ProfileMask);
		}
	}

	return Result;
}

int main(int argc, const char *argv[])
{
    int i,Result = 0;
    parsercontext p;
    textwriter _StdErr;
    stream *Input = NULL;
    tchar_t Path[MAXPATHFULL];
    tchar_t String[MAXLINE],Original[MAXLINE],*s;
    ebml_element *EbmlHead = NULL, *RSegment = NULL, *RLevel1 = NULL, **Cluster;
	ebml_element *EbmlDocVer, *EbmlReadDocVer;
    ebml_element *RSegmentInfo = NULL, *RTrackInfo = NULL, *RChapters = NULL, *RTags = NULL, *RCues = NULL, *RAttachments = NULL, *RSeekHead = NULL;
    ebml_element *WSegment = NULL, *WMetaSeek = NULL;
    matroska_seekpoint *WSeekPoint = NULL;
    ebml_string *LibName, *AppName;
    array RClusters;
    ebml_parser_context RContext;
    ebml_parser_context RSegmentContext;
    int UpperElement;
    filepos_t MetaSeekBefore, MetaSeekAfter;
    filepos_t NextPos, SegmentSize = 0;
	bool_t KeepCues = 0, CuesCreated = 0;
	uint8_t Test[5] = {0x77, 0x65, 0x62, 0x6D, 0};
	int MatroskaProfile = 0;

    // Core-C init phase
    ParserContext_Init(&p,NULL,NULL,NULL);
	StdAfx_Init((nodemodule*)&p);
    ProjectSettings((nodecontext*)&p);

    // EBML & Matroska Init
    MATROSKA_Init((nodecontext*)&p);

    ArrayInit(&RClusters);

    StdErr = &_StdErr;
    memset(StdErr,0,sizeof(_StdErr));
    StdErr->Stream = (stream*)NodeSingleton(&p,STDERR_ID);

    if (argc < 2)
    {
        TextWrite(StdErr,T("mkvalidator v") PROJECT_VERSION T(", Copyright (c) 2010 Matroska Foundation\r\n"));
        Result = OutputError(1,T("Usage: mkclean <matroska_src>"));
        goto exit;
    }

    Node_FromStr(&p,Path,TSIZEOF(Path),argv[argc-1]);
    Input = StreamOpen(&p,Path,SFLAG_RDONLY/*|SFLAG_BUFFERED*/);
    if (!Input)
    {
        TextPrintf(StdErr,T("Could not open file \"%s\" for reading\r\n"),Path);
        Result = -2;
        goto exit;
    }

    // parse the source file to determine if it's a Matroska file and determine the location of the key parts
    RContext.Context = &MATROSKA_ContextStream;
    RContext.EndPosition = INVALID_FILEPOS_T;
    RContext.UpContext = NULL;
    EbmlHead = EBML_FindNextElement(Input, &RContext, &UpperElement, 1);
    if (!EbmlHead)
    {
        Result = OutputError(3,T("Could not find an EBML head"));
        goto exit;
    }

	if (EBML_ElementReadData(EbmlHead,Input,&RContext,0,SCOPE_ALL_DATA)!=ERR_NONE)
    {
        Result = OutputError(4,T("Could not read the EBML head"));
        goto exit;
    }

	CheckUnknownElements(EbmlHead);

	RLevel1 = EBML_MasterFindFirstElt(EbmlHead,&EBML_ContextReadVersion,1,1);
	if (EBML_IntegerValue(RLevel1) > EBML_MAX_VERSION)
		OutputError(5,T("The EBML read version is not supported: %d"),(int)EBML_IntegerValue(RLevel1));

	RLevel1 = EBML_MasterFindFirstElt(EbmlHead,&EBML_ContextMaxIdLength,1,1);
	if (EBML_IntegerValue(RLevel1) > EBML_MAX_ID)
		OutputError(6,T("The EBML max ID length is not supported: %d"),(int)EBML_IntegerValue(RLevel1));

	RLevel1 = EBML_MasterFindFirstElt(EbmlHead,&EBML_ContextMaxSizeLength,1,1);
	if (EBML_IntegerValue(RLevel1) > EBML_MAX_SIZE)
		OutputError(7,T("The EBML max size length is not supported: %d"),(int)EBML_IntegerValue(RLevel1));

	RLevel1 = EBML_MasterFindFirstElt(EbmlHead,&EBML_ContextDocType,1,1);
    Node_FromStr(Input,String,TSIZEOF(String),((ebml_string*)RLevel1)->Buffer);
    if (tcscmp(String,T("matroska"))!=0 && memcmp(((ebml_string*)RLevel1)->Buffer,Test,5)!=0)
	{
		Result = OutputError(8,T("The EBML doctype is not supported: %s"),String);
		goto exit;
	}

	EbmlDocVer = EBML_MasterFindFirstElt(EbmlHead,&EBML_ContextDocTypeVersion,1,1);
	EbmlReadDocVer = EBML_MasterFindFirstElt(EbmlHead,&EBML_ContextDocTypeReadVersion,1,1);

	if (EBML_IntegerValue(EbmlDocVer) > EBML_IntegerValue(EbmlReadDocVer))
		OutputError(9,T("The read DocType version %d is higher than the Doctype version %d"),EBML_IntegerValue(EbmlReadDocVer),EBML_IntegerValue(EbmlDocVer));

	if (tcscmp(String,T("matroska"))==0)
	{
		if (EBML_IntegerValue(EbmlReadDocVer)==2)
			MatroskaProfile = PROFILE_MATROSKA_V2;
		else if (EBML_IntegerValue(EbmlReadDocVer)==1)
			MatroskaProfile = PROFILE_MATROSKA_V1;
		else
			Result |= OutputError(10,T("Unknown Matroska profile %d/%d"),EBML_IntegerValue(EbmlDocVer),EBML_IntegerValue(EbmlReadDocVer));
	}
	else if (EBML_IntegerValue(EbmlReadDocVer)==1)
		MatroskaProfile = PROFILE_TEST;

	if (MatroskaProfile==0)
		Result |= OutputError(11,T("Matroska profile not supported"));

	// find the segment
	RSegment = EBML_FindNextElement(Input, &RContext, &UpperElement, 1);
    RSegmentContext.Context = &MATROSKA_ContextSegment;
    RSegmentContext.EndPosition = EBML_ElementPositionEnd(RSegment);
    RSegmentContext.UpContext = &RContext;
	UpperElement = 0;
//TextPrintf(StdErr,T("Loading the level1 elements in memory\r\n"));
    RLevel1 = EBML_FindNextElement(Input, &RSegmentContext, &UpperElement, 1);
    while (RLevel1)
	{
        if (RLevel1->Context->Id == MATROSKA_ContextCluster.Id)
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,0,SCOPE_PARTIAL_DATA)==ERR_NONE)
			{
                ArrayAppend(&RClusters,&RLevel1,sizeof(RLevel1),256);
				CheckUnknownElements(RLevel1);
				Result |= CheckProfileViolation(RLevel1, MatroskaProfile);
				Result |= CheckMandatory(RLevel1, MatroskaProfile);
			}
			else
			{
				Result = OutputError(0x180,T("Failed to read the Cluster at %lld size %lld"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				goto exit;
			}
        }
        else if (RLevel1->Context->Id == MATROSKA_ContextSeekHead.Id)
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA)==ERR_NONE)
			{
                RSeekHead = RLevel1;
				CheckUnknownElements(RLevel1);
				Result |= CheckProfileViolation(RLevel1, MatroskaProfile);
				Result |= CheckMandatory(RLevel1, MatroskaProfile);
			}
			else
			{
				Result = OutputError(0x100,T("Failed to read the SeekHead at %lld size %lld"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				goto exit;
			}
		}
        else if (RLevel1->Context->Id == MATROSKA_ContextSegmentInfo.Id)
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA)==ERR_NONE)
			{
				if (RSegmentInfo != NULL)
					Result |= OutputError(0x110,T("Extra SegmentInfo found at %lld (size %lld)"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				else
				{
					RSegmentInfo = RLevel1;
					CheckUnknownElements(RLevel1);
					Result |= CheckProfileViolation(RLevel1, MatroskaProfile);
					Result |= CheckMandatory(RLevel1, MatroskaProfile);
				}
			}
			else
			{
				Result = OutputError(0x111,T("Failed to read the SegmentInfo at %lld size %lld"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				goto exit;
			}
		}
        else if (RLevel1->Context->Id == MATROSKA_ContextTracks.Id)
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA)==ERR_NONE)
			{
				if (RTrackInfo != NULL)
					Result |= OutputError(0x120,T("Extra TrackInfo found at %lld (size %lld)"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				else
				{
					RTrackInfo = RLevel1;
					CheckUnknownElements(RLevel1);
					Result |= CheckProfileViolation(RLevel1, MatroskaProfile);
					Result |= CheckMandatory(RLevel1, MatroskaProfile);
				}
			}
			else
			{
				Result = OutputError(0x121,T("Failed to read the TrackInfo at %lld size %lld"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				goto exit;
			}
		}
        else if (RLevel1->Context->Id == MATROSKA_ContextCues.Id)
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA)==ERR_NONE)
			{
				if (RCues != NULL)
					Result |= OutputError(0x130,T("Extra Cues found at %lld (size %lld)"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				else
				{
					RCues = RLevel1;
					CheckUnknownElements(RLevel1);
					Result |= CheckProfileViolation(RLevel1, MatroskaProfile);
					Result |= CheckMandatory(RLevel1, MatroskaProfile);
				}
			}
			else
			{
				Result = OutputError(0x131,T("Failed to read the Cues at %lld size %lld"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				goto exit;
			}
		}
        else if (RLevel1->Context->Id == MATROSKA_ContextChapters.Id)
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA)==ERR_NONE)
			{
				if (RChapters != NULL)
					Result |= OutputError(0x140,T("Extra Chapters found at %lld (size %lld)"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				else
				{
					RChapters = RLevel1;
					CheckUnknownElements(RLevel1);
					Result |= CheckProfileViolation(RLevel1, MatroskaProfile);
					Result |= CheckMandatory(RLevel1, MatroskaProfile);
				}
			}
			else
			{
				Result = OutputError(0x141,T("Failed to read the Chapters at %lld size %lld"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				goto exit;
			}
		}
        else if (RLevel1->Context->Id == MATROSKA_ContextTags.Id)
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA)==ERR_NONE)
			{
				if (RTags != NULL)
					Result |= OutputError(0x150,T("Extra Tags found at %lld (size %lld)"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				else
				{
					RTags = RLevel1;
					CheckUnknownElements(RLevel1);
					Result |= CheckProfileViolation(RLevel1, MatroskaProfile);
					Result |= CheckMandatory(RLevel1, MatroskaProfile);
				}
			}
			else
			{
				Result = OutputError(0x151,T("Failed to read the Tags at %lld size %lld"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				goto exit;
			}
		}
        else if (RLevel1->Context->Id == MATROSKA_ContextAttachments.Id)
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA)==ERR_NONE)
			{
				if (RAttachments != NULL)
					Result |= OutputError(0x160,T("Extra Attachments found at %lld (size %lld)"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				else
				{
					RAttachments = RLevel1;
					CheckUnknownElements(RLevel1);
					Result |= CheckProfileViolation(RLevel1, MatroskaProfile);
					Result |= CheckMandatory(RLevel1, MatroskaProfile);
				}
			}
			else
			{
				Result = OutputError(0x161,T("Failed to read the Attachments at %lld size %lld"),(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
				goto exit;
			}
		}
		else
		{
			if (Node_IsPartOf(RLevel1,EBML_DUMMY_ID))
			{
				tchar_t Id[32];
				EBML_IdToString(Id,TSIZEOF(Id),RLevel1->Context->Id);
				Result |= OutputError(0x80,T("Unknown element %s at %lld size %lld"),Id,(long)RLevel1->ElementPosition,(long)RLevel1->DataSize);
			}
			EBML_ElementSkipData(RLevel1, Input, &RSegmentContext, NULL, 1);
            NodeDelete((node*)RLevel1);
		}
		RLevel1 = EBML_FindNextElement(Input, &RSegmentContext, &UpperElement, 1);
	}

	if (Result==0)
		TextWrite(StdErr,T("The file appears to be valid.\r\n"));

exit:
    for (Cluster = ARRAYBEGIN(RClusters,ebml_element*);Cluster != ARRAYEND(RClusters,ebml_element*); ++Cluster)
        NodeDelete((node*)*Cluster);
    ArrayClear(&RClusters);
    if (RAttachments)
        NodeDelete((node*)RAttachments);
    if (RTags)
        NodeDelete((node*)RTags);
    if (RCues)
        NodeDelete((node*)RCues);
    if (RChapters)
        NodeDelete((node*)RChapters);
    if (RTrackInfo)
        NodeDelete((node*)RTrackInfo);
    if (RSegmentInfo)
        NodeDelete((node*)RSegmentInfo);
    if (RLevel1)
        NodeDelete((node*)RLevel1);
    if (RSegment)
        NodeDelete((node*)RSegment);
    if (EbmlHead)
        NodeDelete((node*)EbmlHead);
    if (Input)
        StreamClose(Input);

    // EBML & Matroska ending
    MATROSKA_Done((nodecontext*)&p);

    // Core-C ending
	StdAfx_Done((nodemodule*)&p);
    ParserContext_Done(&p);

    return Result;
}