# We need HTMLizing certain fields 

html_sanitize()
{
    name="$1"
    val="$2"

    new_val=`echo "$val" | sed -e 's/</\&lt;/g;s/>/\&gt;/g'`

    code="$name"_HTML='"$new_val"'
    eval "$code"
    eval "export $name"_HTML

}

html_sanitize SESSION_LOG "$SESSION_LOG"
html_sanitize SENDTO "$SENDTO"
