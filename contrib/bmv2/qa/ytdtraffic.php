
<?php
include "../local/top.php";
include "../local/connect.php";

$query = "SELECT count(*), sum(duration)/60, sum(cost)/100 from 
        voipcall where disconnecttime::timestamp between current_date - 1 and current_date ;";

print("<H1 align =\"center\"/>Traffic yesterday <br> </H1>");
	print("<p></p>");
	print("<p></p>");

$result = pg_query($connection, $query) 
	or die(" Unable to query the database ");


	$rows = pg_num_rows($result);

	$calls = pg_result($result, 0, 0);
	$duration = substr(pg_result($result, 0, 1), 0, 5);
	$cost = substr(pg_result($result, 0, 2), 0, 5);

	print("<H2 align=\"center\">Total number of calls: $calls  <br> </H2>");
	print("<H2 align=\"center\">Total Duration of calls: $duration  (mins) <br> </H2>");
	print("<H2 align=\"center\">Total Cost of calls (Eur): $cost  <br> </H2>");
	print("<p></p>");
	print("<p></p>");


$query = "SELECT h323id, tariffdesc, count(*), sum(duration)/60, sum(cost)/100 from voipcall 
        where disconnecttime::timestamp between current_date - 1 and current_date 
        group by tariffdesc, h323id ;";
	$result = pg_query($connection, $query) 
		or die(" Unable to query the database ");

	$rows = pg_num_rows($result);

	print("<table frame=border align=\"center\">");
	print("<tr><td>Originator &nbsp; <td> Called &nbsp;  ");
	print("<td> Calls &nbsp; <td>Duration (mins) &nbsp;<td>Cost  </tr> ");

	for($li = 0; $li < $rows ; $li = $li+1) 
	{
	$origin = pg_result($result, $li, 0);
	$carrier = pg_result($result, $li, 1);
	$calls = pg_result($result, $li, 2);
	$duration = number_format(pg_result($result, $li, 3), 2);
	$cost = number_format(pg_result($result, $li, 4), 2);
	print("<tr><td>$origin &nbsp; <td> $carrier &nbsp;  ");
	print("<td> $calls &nbsp; <td>$duration &nbsp; <td> $cost &nbsp; </tr> ");
	}
	print("</table>");

pg_close($connection);
include "../local/top.php";

?>

