/* script.c  version 0.13.26c

  part of rutty - a modified version of putty
  Copyright 2013, Ernst Dijk
*/


/* proto's 

   todo:
   more from script.h should be here
*/
void script_setsend(ScriptData * scriptdata);


/* init scriptdata structure
   copy/translate settings from scripting panel

   todo:
   line_delay and char_delay are in milliseconds - must be converted to ticks
*/
void script_init(ScriptData * scriptdata, Config * cfg)
{
    scriptdata->line_delay = (cfg->script_line_delay<5)?5:cfg->script_line_delay;  /* min.delay 5 ms */
    scriptdata->char_delay = (cfg->script_char_delay);
    scriptdata->cond_char = (cfg->script_cond_line[0]=='\0')?':':cfg->script_cond_line[0];  /* if none use default */
    scriptdata->enable = cfg->script_enable;
    scriptdata->cond_use = cfg->script_enable?cfg->script_cond_use:FALSE;  /* can only be true if wait is enabled */
    scriptdata->except = cfg->script_except;
    scriptdata->timeout = cfg->script_timeout * TICKSPERSEC ;  /* in winstuff.h */

    script_cond_set(scriptdata->waitfor, &scriptdata->waitfor_c, cfg->script_waitfor,strlen(cfg->script_waitfor));
    script_cond_set(scriptdata->halton, &scriptdata->halton_c, cfg->script_halton,strlen(cfg->script_halton));

    scriptdata->crlf = cfg->script_crlf;

    scriptdata->waitfor2[0] = '\0';
    scriptdata->waitfor2_c = -1;  /* -1= there is no condition from file, 0= there an empty line (cr/lf) */

    scriptdata->runs = FALSE;
    scriptdata->send = FALSE;
    scriptdata->filebuffer = NULL;

    scriptdata->latest = 0;

    scriptdata->remotedata_c = script_cond_size;
    scriptdata->remotedata[0] = '\0' ;

    scriptdata->localdata_c = 0 ;
    scriptdata->localdata[0] = '\0' ;
}


/* send script file to host
   0.13: script file often short - read complete file im memory
   makes read a head to detect lf lf possible
*/
BOOL script_sendfile(ScriptData * scriptdata, Filename * script_filename)
 {
    FILE * fp;
    long fsize;
    if(scriptdata->runs)
      return FALSE;  /* a script is already running */

    fp = f_open(*script_filename, "rb", FALSE);
    if(fp==NULL)
    {
      logevent(NULL, "script file not found");
        return FALSE;  /* scriptfile not found or something like it */
    }

    scriptdata->runs = TRUE;
    script_menu(scriptdata);

    fseek(fp, 0L, SEEK_END);
    fsize = ftell(fp);  /* script file size */
    fseek(fp, 0L, SEEK_SET);

    scriptdata->nextnextline = scriptdata->filebuffer = smalloc(fsize);
    scriptdata->filebuffer_end = &scriptdata->filebuffer[fsize];

    if(fread(scriptdata->filebuffer,sizeof(char),fsize,fp)!=fsize)
    {
      logevent(NULL, "script file read failed");
      fclose(fp);
      script_close(scriptdata);
      return FALSE;
    }

    fclose(fp);

    logevent(NULL, "sending script to host ...");

    script_getline(scriptdata);
    script_chkline(scriptdata);

    if(scriptdata->enable && !scriptdata->except)  /* start timeout if wait for prompt is enabled */
    {
      scriptdata->send = FALSE;
      scriptdata->latest = schedule_timer(scriptdata->timeout, script_timeout, scriptdata);
    }
    else
    {
      scriptdata->send = TRUE;
      schedule_timer(scriptdata->line_delay, script_sendline, scriptdata);
    }
    return TRUE;
 }


 /* find nextline in buffer
  */
 int script_findline(ScriptData * scriptdata)
{
    if(scriptdata->filebuffer==NULL)
      return FALSE;

    if(scriptdata->nextnextline >= scriptdata->filebuffer_end)  /* end of filebuffer */
      return FALSE;

    scriptdata->nextline = scriptdata->nextnextline;
    scriptdata->nextline_c = 0;
    while(scriptdata->nextnextline < scriptdata->filebuffer_end && scriptdata->nextnextline[0] !='\n')
    {
      scriptdata->nextnextline++;
      scriptdata->nextline_c++;
    }

    if(scriptdata->nextnextline < scriptdata->filebuffer_end)
    {
      /* while loop ended due lf found, correct pointers to point to next nextline */
      scriptdata->nextnextline++;
      scriptdata->nextline_c++;

      /* cr, lf and crlf are recorded as crlf, lflf and crlflflf
        correct pointers so nextnextline points to the next nextline
        and nextline_c is the size of nextline
        in script file      real data
        data    lf          data
        data    lf lf       data lf     ** correct pointer +1
        data cr lf          data cr
        data cr lf lf lf    data cr lf  ** correct pointer +2
        data cr lf lf       data cr lf  ** this can't be recorded, user edit
        1:   -2 -1  0 +1
        2:   -3 -2 -1  0
      */
      if(scriptdata->crlf==SCRIPT_REC)
      {
        /* 1: not past end of buffer and char is a lf */
        if(scriptdata->nextnextline < scriptdata->filebuffer_end && scriptdata->nextnextline[0]=='\n')
        {
          scriptdata->nextnextline++;
          scriptdata->nextline_c++;

          /* 2: not before buffer start and char at position -3 is a cr
             also not past buffer end and char is a lf
          */
          if( &scriptdata->nextnextline[-3] >= scriptdata->filebuffer && scriptdata->nextnextline[-3]=='\r'
              && scriptdata->nextnextline < scriptdata->filebuffer_end && scriptdata->nextnextline[0]=='\n' )
            {
              scriptdata->nextnextline++;
              //scriptdata->nextline_c++;
            }
        }
        /* correct nextlinesize, remove the lf we added while recording */
        scriptdata->nextline_c--;
      }
    }
    return TRUE;
}


/* read a line from script file
   skip comment/condition lines if conditions are disabled
   if conditions are enabled skip only comments
   e.g. if ':' is the condition marker:
   :condition, the prompt from host were we will wait for
   ::comment, a comment line
*/
void script_getline(ScriptData * scriptdata)
{
    int neof;
    int i;

    if(!scriptdata->runs || scriptdata->filebuffer==NULL)
      return;

    do {
      do
        neof=script_findline(scriptdata);
      while(neof && ( !scriptdata->cond_use && scriptdata->nextline[0]==scriptdata->cond_char
                      || ( scriptdata->cond_use && scriptdata->nextline[0]==scriptdata->cond_char
                           && scriptdata->nextline[1]==scriptdata->cond_char ) ) ) ;
      if(!neof)
      {
        scriptdata->nextline_c = 0;
        scriptdata->nextline_cc = 0;
        return;
      }
      
      i = scriptdata->nextline_c ;
      switch(scriptdata->crlf)  /* translate cr/lf */
      {
        case SCRIPT_OFF: /* no translation */
          break;

        case SCRIPT_NOLF:  /* remove LF (recorded file)*/
          if(scriptdata->nextline[i-1]=='\n')
            i--;
          break;

        case SCRIPT_CR: /* replace cr/lf by cr */
          if(scriptdata->nextline[i-1]=='\n')
            i--;
          if(i>0 && scriptdata->nextline[i-1]=='\r')
            i--;
          scriptdata->nextline[i++]='\r';
          break;

        default:
          break;
      }
    } while(i==0);  /* skip empty lines */
    scriptdata->nextline_c = i;
    scriptdata->nextline_cc = 0;
}


/*  close script file
 */
void script_close(ScriptData * scriptdata)
{
    scriptdata->runs=FALSE;

    expire_timer_context(scriptdata);  /* seems it dusn't work in windows */
    scriptdata->latest = 0;  /* so block previous timeouts this way */

    script_menu(scriptdata);

    if(scriptdata->filebuffer!=NULL)
    {
      sfree(scriptdata->filebuffer);
      scriptdata->filebuffer = NULL;
    }
}


/* send line, called by timer after linedelay
*/
void script_sendline(void *ctx, long now)
{
    ScriptData *scriptdata = (ScriptData *) ctx;

    if(!scriptdata->runs)  /* script terminated */
      return;

    if(scriptdata->nextline_c==0) /* no more lines */
    {
      script_close(scriptdata);
      logevent(NULL, " ...finished sending script");
      return;
    }

    if(scriptdata->char_delay>1)
    {
      schedule_timer(scriptdata->char_delay, script_sendchar, scriptdata);
      return;
    }

    if(scriptdata->char_delay==0)
      ldisc_send(ldisc, scriptdata->nextline, scriptdata->nextline_c, 0);
    else
    {
      int i;
      for(i=0;i<scriptdata->nextline_c;i++)
        ldisc_send(ldisc, &scriptdata->nextline[i], 1, 0);
    }

    script_getline(scriptdata);
    script_chkline(scriptdata);

    if(scriptdata->enable)
    {
      scriptdata->send = FALSE;
      scriptdata->latest = schedule_timer(scriptdata->timeout, script_timeout, scriptdata);
    }
    else
    {
      schedule_timer(scriptdata->line_delay, script_sendline, scriptdata);
    }
    return;
}


/* send char, called by timer after char_delay
*/
void script_sendchar(void *ctx, long now)
{
    ScriptData *scriptdata = (ScriptData *) ctx;

    if(!scriptdata->runs)  /* script terminated */
      return;

    if(scriptdata->nextline_c==0) /* no more lines */ /* can never happen?  it's captured by send line */
    {
      script_close(scriptdata);
      logevent(NULL, "....finished sending script");
      return;
    }

    /* send char */
    if(scriptdata->nextline_cc < scriptdata->nextline_c)
      ldisc_send(ldisc, &scriptdata->nextline[scriptdata->nextline_cc++], 1, 0);

   /* set timer for next */
   if(scriptdata->nextline_cc < scriptdata->nextline_c)
   {
     schedule_timer(scriptdata->char_delay, script_sendchar, scriptdata);
     return;
   }

   /* last char - get next line and set timer */
   script_getline(scriptdata);
   script_chkline(scriptdata);

   if(scriptdata->enable)
    {
      scriptdata->send = FALSE;
      scriptdata->latest = schedule_timer(scriptdata->timeout, script_timeout, scriptdata);
    }
    else
    {
      schedule_timer(scriptdata->line_delay, script_sendline, scriptdata);
    }
   return;
 }


/* called by timer after wait for prompt timeout
*/
void script_timeout(void *ctx, long now)
{
    ScriptData * scriptdata = (ScriptData *) ctx;

    /* disable timer seems not to be working, timeout is disabled by keeping track of time */
    if(abs(now - scriptdata->latest)<50)
    {
      script_close(scriptdata);
      script_fail("script timeout !");
    }
}


/* check line in nextline buffer
   if it's a condition copy/translate it to 'waitfor' and read the nextline
 */
int script_chkline(ScriptData * scriptdata)
{
    if(scriptdata->nextline_c>0 && scriptdata->nextline[0]==scriptdata->cond_char)
    {
      script_cond_set(scriptdata->waitfor2,&scriptdata->waitfor2_c,&scriptdata->nextline[1],scriptdata->nextline_c-1);
      script_getline(scriptdata);
      return TRUE;
    }
    else
    {
      scriptdata->waitfor2_c = -1;
      scriptdata->waitfor2[0] = '\0';
    }
    return FALSE;
}


/* copy condition from settings or scriptfile to scriptdata structure
   there are 2 options:
   condition line  - the complete line must mach before the script is continued or halted
   "word1" "word2" - if one of these words is found the script is continued or halted
                     note: the first char must be "

   'waitfor' and 'halton' are string lists, strings seperated by \0
   lateron we compare it backwards, from end to start, to make that easier the terminating \0 is at start !
*/
void script_cond_set(char * cond, int *p, char *in, int sz)
{
    int i = 0;
    (*p) = 0;

    while(sz>0 && (in[sz-1] =='\n' || in[sz-1] =='\r'))  /* remove cr/lf */
       sz--;

    if(sz==0)
    {
      cond[*p]='\0';
    }
    else if(in[0]!='"')
    {
      if(sz>script_cond_size)
        i = sz - script_cond_size;  /* line to large - use only last part */
      cond[(*p)++]='\0';
      while(i<sz)
        cond[(*p)++] = in[i++];
    }
    else
    {
      if(sz>script_cond_size)
        sz = script_cond_size;  /* word list to large, use only first part */
      i++;
      while(i<sz)
      {
        cond[(*p)++] = '\0';
        while(i<sz && in[i]!='"')
          cond[(*p)++] = in[i++];
        i++;
        while(i<sz && in[i]==' ')
          i++;
      }
    }
}


/* compare received 'data' with our condition list 'ref'
   'ref' is a list of words, from end to start, terminated with \0
   'dc' and 'rc' points to the end+1
   if 'data' has an \0 in it compare will fail
*/
int script_cond_chk(char *ref, int rc, char *data, int dc)
{
    int rcc = rc;
    int dcc = dc;

    while(rcc>0 && dcc>0)
    {
      do {
           rcc--;
           dcc--;
      } while (rcc>=0 && dcc>=0 && ref[rcc]==data[dcc]);

      if(ref[rcc]=='\0')
        return TRUE;

      /* no match - find next word in list*/
      dcc = dc;
      while(rcc>0 && ref[--rcc]!='\0')
        ; /* all done in loop */
    }
    return FALSE;
}


/* setup send line
*/
void script_setsend(ScriptData * scriptdata)
{
    expire_timer_context(scriptdata);  /* stop timeout */
    scriptdata->latest = 0;

    if(scriptdata->nextline_c == 0)  /* no more data */
    {
      script_close(scriptdata);
      logevent(NULL, "... finished sending script");
      return;
    }

    if(script_chkline(scriptdata))  /* new condition - restart timeout */
    {
      scriptdata->send = FALSE;
      scriptdata->latest = schedule_timer(scriptdata->timeout, script_timeout, scriptdata);
    }
    else  /* data - set line delay timer */
    {
      scriptdata->send = TRUE;
      schedule_timer(scriptdata->line_delay, script_sendline, scriptdata);
    }
}


/* capture data from host
   check if thats were we are waiting for
*/
void script_remote(ScriptData * scriptdata, const char * data, int len)
{
  int i = 0 ;
  for (;i<len;i++)
  {
    if(data[i]=='\n' || data[i]=='\r' || data[i]=='\0' )
    {
      /* no need to record cr/lf - we add a lf when writing it to file */
      /* we must prevent recording empty lines - incase cr is followed by lf */
      if(scriptdata->remotedata_c>script_cond_size)
        script_record_line(scriptdata, TRUE);

      /* reset buffer */
      scriptdata->remotedata_c = script_cond_size ;
      scriptdata->remotedata[scriptdata->remotedata_c] = '\0' ;
    }
    else
    {
      /* if buffer full, copy last 'script_cond_size' bytes to first part of the buffer */
      if(scriptdata->remotedata_c>=script_line_size)
      {
        int j = script_line_size - script_cond_size;
        script_record_line(scriptdata, TRUE);
        scriptdata->remotedata_c = 0;
        while(scriptdata->remotedata_c < script_cond_size)
          scriptdata->remotedata[scriptdata->remotedata_c++]=scriptdata->remotedata[j++];
      }
      scriptdata->remotedata[scriptdata->remotedata_c++]=data[i];
    }

    if (scriptdata->runs)
    {
      /* test for halton */
      if(scriptdata->halton_c > 0 && script_cond_chk(scriptdata->halton,scriptdata->halton_c,scriptdata->remotedata,scriptdata->remotedata_c))
      {
        script_close(scriptdata);
        logevent(NULL, "script halted");
        script_fail("script halted");
        return;
      }

      /* test for waitfor, e.g. the prompt to send the next line */
      if(scriptdata->enable && !scriptdata->send)
      {
        if(scriptdata->waitfor2_c >= 0)  /* use prompt from script file */
        {
          if( scriptdata->waitfor2_c == 0 || script_cond_chk(scriptdata->waitfor2,scriptdata->waitfor2_c,scriptdata->remotedata,scriptdata->remotedata_c) )
            script_setsend(scriptdata);
        }
        else if( scriptdata->waitfor_c == 0 || script_cond_chk(scriptdata->waitfor,scriptdata->waitfor_c,scriptdata->remotedata,scriptdata->remotedata_c) )
        {
          script_setsend(scriptdata);
        }
      }
    }
  }
}


 /* capture data entered by the user
 */
void script_local(ScriptData * scriptdata, const char * data, int len)
{
    int i = 0 ;
    if(len<0) len = strlen(data);
    for (;i<len;i++)
    {
      if(scriptdata->localdata_c>=script_line_size)
      {
        /* buffer full, data is recorded as a seperate line
           replay with 'wait for host response' enabled will probetly not be possible without edditing the file
        */
        script_record_line(scriptdata, FALSE);
        scriptdata->localdata_c = 0;
        scriptdata->localdata[scriptdata->localdata_c] = '\0' ;
      }

      if(data[i]=='\r' || data[i]=='\n')
      {
        /* we need to record all data, including cr/lf
           a lf is also used to mark the end of a line in the file
           a lf entered by the user is recorded as lf lf, and a cr as cr lf
        */
        scriptdata->localdata[scriptdata->localdata_c++]=data[i];
        script_record_line(scriptdata, FALSE);
        scriptdata->localdata_c = 0 ;
        scriptdata->localdata[scriptdata->localdata_c] = '\0' ;
      }
      else
      {
        scriptdata->localdata[(scriptdata->localdata_c)++]=data[i];
      }
    }
}


/* start script recording
*/
BOOL script_record(ScriptData * scriptdata, Filename * script_filename)
{
    if(scriptdata->scriptrecord != NULL)
      return FALSE;

    if ((scriptdata->scriptrecord = f_open(*script_filename, "r", FALSE))!=NULL)
    {
      fclose(scriptdata->scriptrecord);
      logevent(NULL, "script recording, file already exists");
      script_fail("script recording, file already exists");
      return FALSE;
    }

    scriptdata->scriptrecord = f_open(*script_filename, "wb", FALSE);
    if(scriptdata->scriptrecord==NULL)
    {
      logevent(NULL, "unable to open file for script recording");
      script_fail("unable to open file for script recording");
        return FALSE;
    }

    /* todo: add 'stop recording' in the menu  */
    logevent(NULL, "script recording started");
    return TRUE;
}


void script_record_stop(ScriptData * scriptdata)
{
    if(scriptdata->scriptrecord!=NULL)
    {
      fclose(scriptdata->scriptrecord);
      scriptdata->scriptrecord = NULL;
      logevent(NULL, "script recording stopped");
      /* todo: remove 'stop recording' from the menu */
    }
};


BOOL script_record_line(ScriptData * scriptdata, int remote)
{
    int fail = FALSE;

    if(scriptdata->scriptrecord == NULL)
      return FALSE;

    if(remote)
    {
      fputc(scriptdata->cond_char, scriptdata->scriptrecord);
      fail = (fwrite(&(scriptdata->remotedata[script_cond_size]), 1, (scriptdata->remotedata_c - script_cond_size), scriptdata->scriptrecord)!=(scriptdata->remotedata_c - script_cond_size));
    }
    else
    {
      fail = (fwrite(scriptdata->localdata, 1, scriptdata->localdata_c, scriptdata->scriptrecord)!=scriptdata->localdata_c);
    }
    if(fail)
    {
      logevent(NULL, "script recording, file write error");
      script_record_stop(scriptdata);
      script_fail("script recording, error writing file");
      return FALSE;
    }

     fputc('\n', scriptdata->scriptrecord);
     fflush(scriptdata->scriptrecord);
     return TRUE;
}


/* end of file */
