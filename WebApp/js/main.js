  function loadInfo() {
	  console.log("Job started.");
	  $.getJSON("http://mpetra.ddns.net/wethermo/info", function( data ) {
		  var items = [];
		  $.each( data, function( key, val ) {
		    if (key != "crono") {
		      items.push( "<li>"+ key + ":" + val + "</li>" );
		    }
		  });
		  $("#mydiv").html(items);
		  console.log(data);
		  console.log("Job done.");
	  });	  
  }
  
  function callOff() {
      $.get("http://mpetra.ddns.net/wethermo/off",function( response ) {
    	  console.log( response ); // server response
	      });
  }
  
  function callAuto() {
	  $.get("http://mpetra.ddns.net/wethermo/auto",function( response ) {
		  console.log( response ); // server response
	      });	  
  }
  
  function callHeat() {
	  $.get("http://mpetra.ddns.net/wethermo/heat",function( response ) {
		  console.log( response ); // server response
		  });	  	  
  }

  function callDisplay() {
          $.get("http://mpetra.ddns.net/wethermo/display",function( response ) {
                  console.log( response ); // server response
                  });
  }
  
  function clearDiv(div) {
	console.log(div);
	$(div).empty();  
  }
