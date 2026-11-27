static void test_send_delivery_ids(void)
{
    ApiWire w={0};NetIface net={&w,wire_connect};RcClient c;RcMsgList echo;const char *reply="{\"success\":true,\"message\":{\"_id\":\"AR-one\",\"rid\":\"r1\",\"msg\":\"kept text\",\"u\":{\"_id\":\"u1\",\"username\":\"me\"},\"ts\":\"2026-11-26T00:00:00Z\"}}";
    buf_init(&w.response);buf_init(&w.request);rc_msglist_init(&echo);
    CHECK(rc_init(&c,&net,"http://chat.test",0)==RC_OK);strcpy(c.user_id,"u1");
    /* Lost acknowledgment: one POST, with a caller-owned ID that survives retry. */
    CHECK(rc_send_id(&c,"r1","kept text","AR-one",&echo)==RC_ERR_NET);
    CHECK(w.connects==1 && strstr(buf_str(&w.request),"\"_id\":\"AR-one\"") && !echo.count);
    wire_response(&w,200,"application/json",reply,strlen(reply));
    CHECK(rc_confirm_send(&c,"r1","kept text","AR-one",&echo)==RC_OK && echo.count==1 && !strcmp(echo.items[0].id,"AR-one"));
    CHECK(strstr(buf_str(&w.request),"GET /api/v1/chat.getMessage?msgId=AR-one") && !strstr(buf_str(&w.request),"POST"));
    rc_msglist_free(&echo);
    wire_response(&w,200,"application/json",reply,strlen(reply));
    CHECK(rc_confirm_send(&c,"other-room","kept text","AR-one",&echo)==RC_ERR_PROTO && !echo.count);
    wire_response(&w,200,"application/json",reply,strlen(reply));
    CHECK(rc_confirm_send(&c,"r1","edited text","AR-one",&echo)==RC_ERR_PROTO && !echo.count);
    strcpy(c.user_id,"different-account");wire_response(&w,200,"application/json",reply,strlen(reply));
    CHECK(rc_confirm_send(&c,"r1","kept text","AR-one",&echo)==RC_ERR_PROTO && !echo.count);
    {int before=w.connects;CHECK(rc_send_id(&c,"r1","kept text","bad\nid",&echo)==RC_ERR_ARGS && before==w.connects);}
    rc_cleanup(&c);rc_msglist_free(&echo);buf_free(&w.response);buf_free(&w.request);
}
