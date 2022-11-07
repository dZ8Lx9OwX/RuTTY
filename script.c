/* script.c  version 0.12

  part of rutty - a modified version of putty
  Copyright 2013, Ernst Dijk
*/


/* proto's 

   todo:
   move internal proto's from script.h
*/
void script_setsend(ScriptData * scriptdata);


/* init scriptdata structure
   copy/translate settings from scripting panel

   todo:
   line_delay and char_delay are in milliseconds - convert to ticks
*/
void script_init(ScriptData * scriptdata, Config * cfg)
{
    scriptdata->line_delay = (cfg->script_line_delay<5)?5:cfg->script_line_delay;  /* min.delay 5 ms */
    scriptdata->char_delay = (cfg->script_char_delay);
    scriptdata->cond_char = (cfg->script_cond_line[0]=='\0')?':':cfg->script_cond_line[0];
    scriptdata->enable = cfg->script_enable;
    scriptdata->cond_use = cfg->script_enable?cfg->script_cond_use:FALSE;  /* can only be true if wait is enabled */
    scriptdata->except = cfg->script_except;
    scriptdata->timeout = cfg->script_timeout * TICKSPERSEC ;  /* in winstuff.h */

    script_cond_set(scriptdata->waitfor, &scriptdata->waitfor_c, cfg->script_waitfor);
    script_cond_set(scriptdata->halton, &scriptdata->halton_c, cfg->script_halton);

    scriptdata->crlf = cfg->script_crlf;

    scriptdata->waitfor2[0] = '\0';
    scriptdata->waitfor2_c = -1;  /* -1= there is no condition from file, 0= there is cr or lf in file */

    scriptdata->runs = FALSE;
    scriptdata->send = FALSE;
    scriptdata->scriptfile = NULL;

    scriptdata->latest = 0;

    scriptdata->remotedata_c = script_cond_size;
    scriptdata->remotedata[0] = '\0';

    scriptdata->localdata_c = 0;
    scriptdata->localdata[0] = '\0';
}


/* send script file to host
*/
BOOL script_sendfile(ScriptData * scriptdata, Filename * script_filename)
 {
    if(scriptdata->runs || scriptdata->scriptfile != NULL)
      return FALSE;  /* a script is already running */

    scriptdata->scriptfile = f_open(*script_filename, "rb", FALSE);
    if(scriptdata->scriptfile==NULL)
    {
      logevent(NULL, "script file not found");
        return FALSE;  /* script file not found or something like it */
    }

    scriptdata->runs = TRUE;
    script_menu(scriptdata);

    script_getline(scriptdata);
    script_chkline(scriptdata);

    logevent(NULL, "sending script to host ...");

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


/* read a line from script file
   skip comment/condition lines if conditions are disabled
   if conditions are enabled skip only comments
   if ':' is the condition marker:
   :condition, the prompt from host were we will wait for
   ::comment, a comment line
*/
void script_getline(ScriptData * scriptdata)
{
    char * ex;
    int i;

    if(!scriptdata->runs || scriptdata->scriptfile==NULL)
      return;

    scriptdata->nextline_c = 0;
    scriptdata->nextline_cc = 0;

    do {
      do
        ex=fgets(scriptdata->nextline,script_line_size,scriptdata->scriptfile);
      while(ex!=NULL && ( !scriptdata->cond_use && scriptdata->nextline[0]==scriptdata->cond_char
                            || ( scriptdata->cond_use && scriptdata->nextline[0]==scriptdata->cond_char
                                 && scriptdata->nextline[1]==scriptdata->cond_char ) ) ) ;
      if(ex==NULL)
        return;

      i = strlen(scriptdata->nextline);
      switch(scriptdata->crlf)  /* translate cr/lf */
      {
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

        case SCRIPT_OFF: /* no translation */
          break;

        default:
          break;
      }
    } while(i==0);  /* skip empty lines */
    scriptdata->nextline_c = i;
}


/*  close script file
 */
void script_close(ScriptData * scriptdata)
{
    scriptdata->runs=FALSE;

    expire_timer_context(scriptdata);  /* seems it dusn't work in windows */
    scriptdata->latest = 0;  /* block previous timeouts this way */

    script_menu(scriptdata);

    if(scriptdata->scriptfile!=NULL)
    {
      fclose(scriptdata->scriptfile);
      scriptdata->scriptfile = NULL;
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

    /* was last char - get next line and set timer */
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

    /* disable timer seems to be not working, timeout is disabled by keeping track of time */
    if(abs(now - scriptdata->latest)<50)
    {
      script_close(scriptdata);
      script_fail("script timeout !");
    }
}


/* check line in nextline buffer
   if it is a condition copy/translate it to 'waitfor' and read the nextline
 */
int script_chkline(ScriptData * scriptdata)
{
    if(scriptdata->nextline_c>0 && scriptdata->nextline[0]==scriptdata->cond_char)
    {
      script_cond_set(scriptdata->waitfor2,&scriptdata->waitfor2_c,&scriptdata->nextline[1]);
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


/* copy condition from scripting panel or scriptfile to scriptdata structure
   there are 2 options:
   condition line  - the complete line must mach before the script is continued or halted
   "word1" "word2" - if one of these words is found the script is continued or halted
                     note: the first char must be "

   'waitfor' and 'halton' are string lists, strings seperated by \0
   lateron we compare it backwards, from end to start, to make that easier the terminating \0 is at start !
*/
void script_cond_set(char * cond, int *p, char *in)
{
    int i = 0;
    int l = strlen(in);
    (*p) = 0;

    while(in[l-1] =='\n' || in[l-1] =='\r')  /* remove cr/lf */
       l--;

    if(l==0)
    {
      cond[*p]='\0';
    }
    else if(in[0]!='"')
    {
      if(l>script_cond_size)
        i = l - script_cond_size;  /* line to large - use only last part */
      cond[(*p)++]='\0';
      while(i<l)
        cond[(*p)++] = in[i++];
    }
    else
    {
      if(l>script_cond_size)
        l = script_cond_size;  /* word list to large, use only first part */
      i++;
      while(i<l)
      {
        cond[(*p)++] = '\0';
        while(i<l && in[i]!='"')
          cond[(*p)++] = in[i++];
        i++;
        while(i<l && in[i]==' ')
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
    if(data[i]=='\n' || data[i]=='\0' || data[i]=='\r')
    {
      /* no need to record cr/lf - we add a lf when writing it to file */
      /* we must prevent recording empty lines - incase cr is followed by lf */
      if(scriptdata->remotedata_c>script_cond_size)
        script_record_line(scriptdata, TRUE);

      //reset buffer
      scriptdata->remotedata_c = script_cond_size ;
      scriptdata->remotedata[scriptdata->remotedata_c] = '\0' ;
    }
    else
    {
      /* if buffer full copy last 'script_cond_size' bytes to first part of the buffer */
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
        /* we record all data, including cr/lf
           a lf is also used to mark the end of a line in the output file
           a lf entered by the user is recorded as lf lf, and a cr as cr lf
        */
        scriptdata->localdata[scriptdata->localdata_c++]=data[i];
        script_record_line(scriptdata, FALSE);
        scriptdata->localdata_c = 0 ;
        scriptdata->localdata[scriptdata->localdata_c] = '\0' ;
      }
      else //if(data[i]!='\0')
      {
        /* \0 in data stream - how to handle ?
           \0 is used as end-of-string
        */
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

    /* todo: add 'stop recording' in the menu */
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
}


BOOL script_record_line(ScriptData * scriptdata, int remote)
{
   int fail = FALSE;

   if(scriptdata->scriptrecord == NULL)
     return FALSE;

   if(remote)
   {
     fputc(scriptdata->cond_char, scriptdata->scriptrecord);
     fail = (fwrite(&(scriptdata->remotedata[script_cond_size]), 1, (scriptdata->remotedata_c - script_cond_size), scriptdata->scriptrecord)!=(scriptdata->remotedata_c - script_cond_size));
     //scriptdata->remotedata[scriptdata->remotedata_c]='\0'; //there might be no space for a eos !
     //fputs(&(scriptdata->remotedata[script_cond_size]),scriptdata->scriptrecord);
   }
   else
   {
     fail = (fwrite(scriptdata->localdata, 1, scriptdata->localdata_c, scriptdata->scriptrecord)!=scriptdata->localdata_c);
     //scriptdata->localdata[scriptdata->localdata_c]='\0';
     //fputs(scriptdata->localdata,scriptdata->scriptrecord);
   }
   if (fail)
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
