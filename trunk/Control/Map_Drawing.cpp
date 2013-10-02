#pragma region Include
#include "stdafx.h"
#include "MapWinGis.h"
#include "Map.h"
#include "Tiles.h"
#include "TilesDrawer.h"
#include "GeoProjection.h"
#include "ShapefileDrawing.h"
#include "ImageDrawing.h"
#include "LabelDrawing.h"
#include "ChartDrawing.h"
#include "Image.h"
#include "Measuring.h"

#pragma endregion

#pragma region OnDraw
// ***************************************************************
//		OnDraw()
// ***************************************************************
void CMapView::OnDraw(CDC* pdc, const CRect& rcBounds, const CRect& rcInvalid)
{
	if (m_isSizing)
	{
		// An option to clear the control surface with back color before redraw while sizing
		// but to leave the the previous bitmap and to paint only the new regions seems nicer
		//m_layerDC->FillSolidRect(rcBounds,m_backColor);
		//ShowRedrawTime(0.0f, "Drawing...");
		//pdc->BitBlt( 0, 0,  rcBounds.Width(), rcBounds.Height(), m_layerDC, 0, 0, SRCCOPY);
		return;	  // redraw is prohibited before the sizing will be finished
	}
	
	//This line is intended to ensure proper function in MSAccess by verifying that the hWnd handle exists
	//before trying to draw. Lailin Chen - 2005/10/17
	if (this->m_hWnd == NULL)
		return;
	
	// no redraw is allowed when the rubber band is being dragged
	if (m_rectTrackerIsActive)
		return;

	bool dragging =  (m_bitbltClickDown.x != 0 || m_bitbltClickDown.y != 0 ||
					  m_bitbltClickMove.x != 0 || m_bitbltClickMove.y != 0);
	
	// for panning restore everything from the main buffer, no need to update anything until user releases the button
	if (dragging)
	{
		// background for the new area
		pdc->FillSolidRect(0, 0, m_bitbltClickMove.x - m_bitbltClickDown.x, rcBounds.Height(), m_backColor);
		pdc->FillSolidRect(rcBounds.Width() - ( m_bitbltClickDown.x - m_bitbltClickMove.x ), 0, rcBounds.Width(), rcBounds.Height(), m_backColor);
		pdc->FillSolidRect(0, 0, rcBounds.Width(), m_bitbltClickMove.y - m_bitbltClickDown.y, m_backColor);
		pdc->FillSolidRect(0, rcBounds.Height() - ( m_bitbltClickDown.y - m_bitbltClickMove.y ), rcBounds.Width(), rcBounds.Height(), m_backColor);
		
		// passing main buffer to the screen
		int x = m_bitbltClickMove.x - m_bitbltClickDown.x;
		int y = m_bitbltClickMove.y - m_bitbltClickDown.y;

		Gdiplus::Graphics* gBuffer = Gdiplus::Graphics::FromImage(m_bufferBitmap);
		
		// drawing layers
		//gBuffer->DrawImage(m_tilesBitmap, (Gdiplus::REAL)0.0, (Gdiplus::REAL)0.0);
		//gBuffer->DrawImage(m_layerBitmap, (Gdiplus::REAL)0.0, (Gdiplus::REAL)0.0);
		//gBuffer->DrawImage(m_drawingBitmap, (Gdiplus::REAL)0.0, (Gdiplus::REAL)0.0);
	
		// blit to the screen
		HDC hdc = pdc->GetSafeHdc();
		Gdiplus::Graphics* g = Gdiplus::Graphics::FromHDC(hdc);
		g->DrawImage(m_bufferBitmap, (Gdiplus::REAL)x, (Gdiplus::REAL)y);
		g->ReleaseHDC(hdc);
		delete g;

		delete gBuffer;
	}
	else
	{
		// the map is locked
		if (m_lockCount > 0)
			return;
		
		if (m_drawMouseMoves && !(m_blockMouseMoves || !m_canbitblt)) {
			this->DrawMouseMoves(pdc, rcBounds, rcInvalid);
			m_drawMouseMoves = false;
		}
		else {
			this->HandleNewDrawing(pdc, rcBounds, rcInvalid);
			m_blockMouseMoves = false;
		}
	}
}

// ***************************************************************
//		DrawMouseMoves()
// ***************************************************************
void CMapView::DrawMouseMoves(CDC* pdc, const CRect& rcBounds, const CRect& rcInvalid) {

	// drawing layers
	Gdiplus::Graphics* gDrawing = Gdiplus::Graphics::FromImage(m_drawingBitmap);			// allocate another bitmap for this purpose
	gDrawing->DrawImage(m_bufferBitmap, 0.0f, 0.0f);

	// update measuring
	if( m_cursorMode == cmMeasure ) {
		CMeasuring* m =((CMeasuring*)m_measuring);
		if (m->points.size() > 0) {
			double x, y;
			this->ProjectionToPixel(m->points[m->points.size() - 1].x, m->points[m->points.size() - 1].y, x, y);

			if (m->points.size() > 0 && (m->mousePoint.x != x || m->mousePoint.y != y)) {
				Gdiplus::Pen pen(Gdiplus::Color::Orange, 2.0f);
				gDrawing->DrawLine(&pen, (Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)m->mousePoint.x, (Gdiplus::REAL)m->mousePoint.y);
			}
		}
	}

	HDC hdc = pdc->GetSafeHdc();
	Gdiplus::Graphics* g = Gdiplus::Graphics::FromHDC(hdc);
	g->SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	g->DrawImage(m_drawingBitmap, 0.0f, 0.0f);

	delete gDrawing;
	g->ReleaseHDC(pdc->GetSafeHdc());
	delete g;
}

// ***************************************************************
//		OnDraw()
// ***************************************************************
void CMapView::HandleNewDrawing(CDC* pdc, const CRect& rcBounds, const CRect& rcInvalid, float offsetX, float offsetY)
{
	#ifdef _DEBUG	
	CLSID pngClsid;
	Utility::GetEncoderClsid(L"image/png", &pngClsid);		
	#endif
	
	bool layersRedraw = !m_canbitblt;
	bool drawingRedraw = true;		// currently always on; can be time consuming when tiles are enabled 
	
	// background
	long alpha = (255)<<24;
	Gdiplus::Color backColor = Gdiplus::Color(alpha | BGR_TO_RGB(m_backColor));

	Gdiplus::Graphics* gBuffer = NULL;
	Gdiplus::Graphics* gPrinting = NULL;
	if (m_isSnapshot)
	{
		//SetBkMode (hdc, TRANSPARENT);
		//SetBkColor (hDC, m_backColor);
		gPrinting = Gdiplus::Graphics::FromHDC(pdc->GetSafeHdc());
		gPrinting->TranslateTransform(offsetX, offsetY);
		Gdiplus::RectF clip(rcInvalid.left, rcInvalid.top, rcInvalid.Width(), rcInvalid.Height());
		gPrinting->SetClip(clip);
		

		//gPrinting->SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		//gPrinting->Clear(backColor);
		
		Gdiplus::Color color(255, 255, 255, 255);
		Gdiplus::SolidBrush brush(color);
		gPrinting->FillRectangle(&brush, 0.0f, 0.0f, (float)1000.0f, (float)1000.0f);
		
		/*Gdiplus::Color color2(100, 255, 0, 0);
		brush.SetColor(color2);
		gPrinting->FillRectangle(&brush, 0.0f, 0.0f, 1000.0f, 1000.0f);*/
	}
	else
	{
		gBuffer = Gdiplus::Graphics::FromImage(m_bufferBitmap);
		gBuffer->SetCompositingMode(Gdiplus::CompositingModeSourceOver);
		gBuffer->Clear(backColor);
	}

	// tiles
	VARIANT_BOOL tilesVisible;
	m_tiles->get_Visible(&tilesVisible);

	// if projection isn't defined there is no way to display tiles
	if (tilesVisible && m_transformationMode != tmNotDefined)		// TODO: restore
	{
		CTiles* tiles = (CTiles*)m_tiles;
		if (m_isSnapshot)
		{
			if (((CTiles*)m_tiles)->TilesAreInScreenBuffer((void*)this))
			{
				((CTiles*)m_tiles)->MarkUndrawn();	
				CTilesDrawer drawer(gPrinting, &this->extents, this->m_pixelPerProjectionX, this->m_pixelPerProjectionY);
				if (m_transformationMode == tmDoTransformation)
					drawer.m_transfomation = ((CGeoProjection*)m_wgsProjection)->m_transformation;
				drawer.DrawTiles(this->m_tiles, this->PixelsPerMapUnit(), m_projection, tiles->m_provider->Projection, true);
				((CTiles*)m_tiles)->MarkUndrawn();
			}
		}
		else
		{
			if (tiles->UndrawnTilesExist())
			{
				Gdiplus::Graphics* gTiles = Gdiplus::Graphics::FromImage(m_tilesBitmap);
				if (!tiles->DrawnTilesExist())
				{
					// if it's the first tile - clear the background
					gTiles->Clear(Gdiplus::Color::Transparent);
				}
				
				// draw new tiles
				CTilesDrawer drawer(gTiles, &this->extents, this->m_pixelPerProjectionX, this->m_pixelPerProjectionY);

				if (m_transformationMode == tmDoTransformation)
					drawer.m_transfomation = ((CGeoProjection*)m_wgsProjection)->m_transformation;

				drawer.DrawTiles(this->m_tiles, this->PixelsPerMapUnit(), m_projection, tiles->m_provider->Projection, false);
			}

			if (tiles->DrawnTilesExist())
			{
				gBuffer->DrawImage(m_tilesBitmap, 0.0f, 0.0f);
			}
		}
	}
	
	// layers
	bool layersExist = m_activeLayers.size() > 0;
	if (layersExist)
	{
		if (m_isSnapshot)
		{
			this->DrawLayers(rcBounds, gPrinting);
		}
		else
		{
			if(!layersRedraw)
			{	
				// update from the layer buffer
				gBuffer->DrawImage(m_layerBitmap, 0.0f, 0.0f);
			}
			else
			{
				DWORD startTick = ::GetTickCount();
				
				Gdiplus::Graphics* gLayers = Gdiplus::Graphics::FromImage(m_layerBitmap);
				gLayers->Clear(Gdiplus::Color::Transparent);

				#ifdef _DEBUG
				//m_layerBitmap->Save(L"C:\\layers.png", &pngClsid, NULL);
				#endif

				gLayers->SetCompositingMode(Gdiplus::CompositingModeSourceOver);

				// main drawing
				//if (m_RotateAngle == 0)	// currently rotation is simply ignored
				{
					 this->DrawLayers(rcBounds, gLayers);
					
					 #pragma region Asynchronous
					 // a pointer ot member function
					 /*UINT (CMapView::* ptrDrawLayers)(LPVOID) = &CMapView::StartDrawLayers;
					 CMapView* map = this;
					 (this->*ptrDrawLayers)(NULL);*/

					 //DrawingParams* param = new  DrawingParams(this, gLayers, &rcBounds);
					 //CWinThread* thread = AfxBeginThread(&StartThread, param);
					 //delete param;
					 #pragma endregion
				}
				
				if (0)	// TODO: reimplement rotation
				{
					// TODO: reimplement using GDI+
					#pragma region Rotation
						//HDC hdcLayers = gLayers->GetHDC();
						//CDC* dcLayers = CDC::FromHandle(hdcLayers);
						//if (dcLayers)
						//{
					
						//	CDC     *tmpBackbuffer = new CDC();
						//	CRect   tmpRcBounds = new CRect();
						//	Extent  tmpExtent, saveExtent;       
						//	long    save_viewWidth, save_viewHeight;

						//	if (m_Rotate == NULL)
						//	  m_Rotate = new Rotate();

						//	tmpBackbuffer->CreateCompatibleDC(dcLayers);
						//	m_Rotate->setSize(rcBounds);
						//	m_Rotate->setupRotateBackbuffer(tmpBackbuffer->m_hDC, pdc->m_hDC, m_backColor);

						//	save_viewWidth = m_viewWidth;
						//	save_viewHeight = m_viewHeight;
						//	m_viewWidth = m_Rotate->rotatedWidth;
						//	m_viewHeight = m_Rotate->rotatedHeight;
						//	saveExtent = extents;
						//	tmpExtent = extents;
						//	tmpExtent.right += (m_Rotate->xAxisDiff * m_inversePixelPerProjectionX);
						//	tmpExtent.bottom -= (m_Rotate->yAxisDiff * m_inversePixelPerProjectionY);
						//	tmpExtent.left -= (m_Rotate->xAxisDiff * m_inversePixelPerProjectionX);
						//	tmpExtent.top += (m_Rotate->yAxisDiff * m_inversePixelPerProjectionY);
						//	extents = tmpExtent;

						//	// draw the Map
						//	//this->DrawLayers(rcBounds,tmpBackbuffer, gLayers);
						//	
						//	// Cleanup
						//	extents = saveExtent;
						//	m_viewWidth = save_viewWidth;
						//	m_viewHeight = save_viewHeight;
						//	m_Rotate->resetWorldTransform(tmpBackbuffer->m_hDC);
						//	dcLayers->BitBlt(0,0,rcBounds.Width(),rcBounds.Height(), tmpBackbuffer, 0, 0, SRCCOPY);
						//	m_Rotate->cleanupRotation(tmpBackbuffer->m_hDC);
						//	tmpBackbuffer->DeleteDC();
						//}
						//gLayers->ReleaseHDC(hdcLayers);
					#pragma endregion
				}
				
				// passing layers to the back buffer
				gBuffer->DrawImage(m_layerBitmap, 0.0f, 0.0f);

				// displaying the time
				DWORD endTick = GetTickCount();
				this->ShowRedrawTime(gBuffer, (float)(endTick - startTick)/1000.0f);
				m_canbitblt = TRUE;
			}
		}
	}
	
	// hot tracking
	if (m_hotTracking.Shapefile && !m_isSnapshot)
	{
		CShapefileDrawer drawer(gBuffer, &extents, m_pixelPerProjectionX, m_pixelPerProjectionY, &m_collisionList, 
			this->GetCurrentScale(), true);
		drawer.Draw(rcBounds, m_hotTracking.Shapefile, ((CShapefile*)m_hotTracking.Shapefile)->get_File());
	}

	// passing buffer to user (includes background, tiles, layers, spatially referenced drawing)
	if (m_sendOnDrawBackBuffer && !m_isSnapshot)
	{
		HDC hdc = gBuffer->GetHDC();
		this->FireOnDrawBackBuffer((long)hdc);
		gBuffer->ReleaseHDC(hdc);
	}
	
	#ifdef _DEBUG
	//m_layerBitmap->Save(L"C:\\layers.png", &pngClsid, NULL);
	//m_bufferBitmap->Save(L"C:\\buffer.png", &pngClsid, NULL);
	#endif

	if (m_isSnapshot)
	{
		this->DrawLists(rcBounds, gPrinting, dlSpatiallyReferencedList);
		this->DrawLists(rcBounds, gPrinting, dlScreenReferencedList);
	}
	else
	{
		// drawing layers
		Gdiplus::Graphics* gDrawing = Gdiplus::Graphics::FromImage(m_drawingBitmap);
		gDrawing->Clear(Gdiplus::Color::Transparent);

		// fire external drawing
		{
			HDC hdc = gBuffer->GetHDC();
			VARIANT_BOOL retVal = VARIANT_FALSE;
			this->FireBeforeDrawing((long)hdc, rcBounds.left, rcBounds.right, rcBounds.top, rcBounds.bottom, &retVal);
			gBuffer->ReleaseHDC(hdc);
		}

		// temp objects
		this->DrawLists(rcBounds, gDrawing, dlSpatiallyReferencedList);
		this->DrawLists(rcBounds, gDrawing, dlScreenReferencedList);
		
		// fire external drawing code
		{
			HDC hdc = gBuffer->GetHDC();
			VARIANT_BOOL retVal = VARIANT_FALSE;
			this->FireAfterDrawing((long)hdc, rcBounds.left, rcBounds.right, rcBounds.top, rcBounds.bottom, &retVal);
			gBuffer->ReleaseHDC(hdc);
		}

		// passing layers to the back buffer
		gBuffer->DrawImage(m_drawingBitmap, 0.0f, 0.0f);
		delete gDrawing;
	}

	if (m_scalebarVisible)
		this->DrawScaleBar(m_isSnapshot ? gPrinting : gBuffer);

	if (m_cursorMode == cmMeasure && !m_isSnapshot)
		this->DrawMeasuring(gBuffer);

	// passing the main buffer to the screen
	if (!m_isSnapshot)
	{
		HDC hdc = pdc->GetSafeHdc();
		Gdiplus::Graphics* g = Gdiplus::Graphics::FromHDC(hdc);
		g->SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		g->DrawImage(m_bufferBitmap, 0.0f, 0.0f);
		g->ReleaseHDC(pdc->GetSafeHdc());
		delete g;
		delete gBuffer;
	}
	else
	{
		gPrinting->ReleaseHDC(pdc->GetSafeHdc());
		delete gPrinting;
	}
}
#pragma endregion

#pragma region Draw layers
// ****************************************************************
//		DrawLayers()
// ****************************************************************
void CMapView::DrawLayers(const CRect & rcBounds, Gdiplus::Graphics* graphics)
{
	if (m_lockCount > 0 && !m_isSnapshot)
	{
		return;
	}
	
	HCURSOR oldCursor = this->SetWaitCursor();

	// clear extents of drawn labels and charts
	this->ClearLabelFrames();
	
	m_drawMutex.Lock();

	//dc->FillSolidRect(rcBounds,m_backColor);
	register int i, j;
	long startcondition = 0;
	long endcondition = m_activeLayers.size();

	if (endcondition == 0)
	{
		m_drawMutex.Unlock();

      if (oldCursor != NULL)
         ::SetCursor(oldCursor);
		return;
	}
	
	// ------------------------------------------------------------------
	//	Check whether some layers are completely concealed by images 
	//	no need to draw them then
	// ------------------------------------------------------------------
	bool * isConcealed = NULL;
	if( endcondition )
	{
		isConcealed = new bool[endcondition];
		memset(isConcealed,0,endcondition*sizeof(bool));
	}
	
	double scale = this->GetCurrentScale();
	int zoom;
	this->m_tiles->get_CurrentZoom(&zoom);

	if( m_numImages > 0 && !_canUseImageGrouping )
	{
		for( i = endcondition - 1; i >= 0; i-- )
		{
			Layer * l = m_allLayers[m_activeLayers[i]];
			if( IS_VALID_PTR(l) )
			{
				if( l->type == ImageLayer && l->IsVisible(scale, zoom)) 
				{
					IImage * iimg = NULL;
					l->object->QueryInterface(IID_IImage,(void**)&iimg);
					if( iimg == NULL )continue;
					
					this->AdjustLayerExtents(i);

					VARIANT_BOOL useTransparencyColor;
					iimg->get_UseTransparencyColor(&useTransparencyColor);
					iimg->Release();
					iimg = NULL;

					if( useTransparencyColor == FALSE )
					{
						//Check if this is the end condition layer
						if( l->extents.left <= extents.left && 
							l->extents.right >= extents.right &&
							l->extents.bottom <= extents.bottom && 
							l->extents.top >= extents.top )
						{	
							startcondition = i;
							break;
						}
						//Check if this layer conceals any others
						else if( isConcealed[i] == false )
						{
							for( j = i - 1; j >= 0; j-- )
							{
								Layer * l2 = m_allLayers[m_activeLayers[j]];
								if( IS_VALID_PTR(l2) )
								{
									if( l->extents.left <= l2->extents.left && 
										l->extents.right >= l2->extents.right &&
										l->extents.bottom <= l2->extents.bottom && 
										l->extents.top >= l2->extents.top )
									{
										isConcealed[j] = true;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	
	// ------------------------------------------------------------------
	//		Drawing of grouped image layers
	// ------------------------------------------------------------------
	if ( _canUseImageGrouping )
	{
		std::vector<ImageGroup*>* newGroups = new std::vector<ImageGroup*>;

		// building groups
		this->BuildImageGroups(*newGroups);
		
		// comparing them with the old list
		if (m_imageGroups != NULL)
		{
			if (this->ImageGroupsAreEqual(*m_imageGroups, *newGroups))
			{
				// groups are the same so we can continue to use them
				for (size_t i = 0; i < newGroups->size(); i++)
				{
					delete (*newGroups)[i];
				}
				newGroups->clear();
				delete newGroups;
				newGroups = NULL;
			}
			else
			{
				// groups has changed, swapping pointers
				if (m_imageGroups != NULL)
				{
					for (size_t i = 0; i < m_imageGroups->size(); i++)
					{
						delete (*m_imageGroups)[i];
					}
					
					m_imageGroups->clear();
					delete m_imageGroups;
					m_imageGroups = NULL;
				}
				m_imageGroups = newGroups;
			}
		}
		else
		{
			m_imageGroups = newGroups;
		}
		
		// mark all images as undrawn
		for (size_t i = 0; i < m_imageGroups->size(); i++)
		{
			(*m_imageGroups)[i]->wasDrawn = false;
		}
	}
	
	// ------------------------------------------------------------------
	//		Actual drawing
	// ------------------------------------------------------------------
	double currentScale = this->GetCurrentScale();

	bool useCommonCollisionListForCharts = true;
	bool useCommonCollisionListForLabels = true;

	// collision avoidance
	m_collisionList.Clear();
	CCollisionList collisionListLabels;
	CCollisionList collisionListCharts;
	
	CCollisionList* chosenListLabels = NULL;
	CCollisionList* chosenListCharts = NULL;
	
	chosenListLabels = useCommonCollisionListForLabels?(&m_collisionList):(&collisionListLabels);
	chosenListCharts = useCommonCollisionListForCharts?(&m_collisionList):(&collisionListCharts);

	// initializing classes for drawing
	bool forceGdiplus = this->m_RotateAngle != 0.0f || m_isSnapshot;
	
	CShapefileDrawer sfDrawer(graphics, &extents, m_pixelPerProjectionX, m_pixelPerProjectionY, &m_collisionList, 
						   this->GetCurrentScale(), forceGdiplus);

	CImageDrawer imgDrawer(graphics, &extents, m_pixelPerProjectionX, m_pixelPerProjectionY, m_viewWidth, m_viewHeight);
	CLabelDrawer lblDrawer(graphics, &extents, m_pixelPerProjectionX, m_pixelPerProjectionY, currentScale, 
		chosenListLabels, m_RotateAngle, m_isSnapshot);
	CChartDrawer chartDrawer(graphics, &extents, m_pixelPerProjectionX, m_pixelPerProjectionY, currentScale, chosenListCharts);

	for(int i = startcondition; i < endcondition; i++)
	{
		//CString str;
		//str.Format("Drawing layer %d", i);
		//timer.PrintTime(str.GetBuffer());
		
		if( isConcealed[i] == false )
		{
			Layer * l = m_allLayers[m_activeLayers[i]];
			if( l != NULL )
			{	
				if (l->IsVisible(scale, zoom))
				{
					if(l->type == ImageLayer)
					{
						if(l->object == NULL ) continue;
						IImage * iimg = NULL;
						l->object->QueryInterface(IID_IImage,(void**)&iimg);
						if( iimg == NULL ) continue;
						
						CImageClass* img = (CImageClass*)iimg;
						
						if (_canUseImageGrouping && img->m_groupID != -1)
						{
							// this is grouped image, if this is the first image of group, we shall draw the whole group
							if (!(*m_imageGroups)[img->m_groupID]->wasDrawn)
							{
								this->DrawImageGroups(rcBounds, graphics, img->m_groupID);
								(*m_imageGroups)[img->m_groupID]->wasDrawn = true;
							}
						}
						else
						{
							bool saveBitmap = true;
							
							if (saveBitmap)
							{
								CImageClass* img = (CImageClass*)iimg;
								ScreenBitmap* bmp = img->_screenBitmap;
								bool wasDrawn = false;

								// in case we have saved bitmap and map position is the same we shall draw it
								if (bmp)
								{
									if (bmp->extents == extents &&
										bmp->pixelPerProjectionX == m_pixelPerProjectionX &&
										bmp->pixelPerProjectionY == m_pixelPerProjectionY &&
										bmp->viewWidth == m_viewWidth &&
										bmp->viewHeight == m_viewHeight && !((CImageClass*)iimg)->_imageChanged )
									{
										//Gdiplus::Graphics g(dc->m_hDC);
										graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
										
										// TODO: choose interpolation mode more precisely
										// TODO: set image attributes
										
										graphics->SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);	
										graphics->SetSmoothingMode(Gdiplus::SmoothingModeDefault);
										graphics->SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
										graphics->DrawImage(bmp->bitmap, Gdiplus::REAL(bmp->left), Gdiplus::REAL(bmp->top));
										wasDrawn = true;
									}
								}
								
								if (!wasDrawn)
								{
									long width, height;
									iimg->get_OriginalWidth(&width);
									iimg->get_OriginalHeight(&height);

									if ((width == 256 && height == 256) || m_isSnapshot)
									{
										// it's tiles, I don't want to cache bitmap here to avoid seams
										// the same thing with Snapshot calls
										bmp = imgDrawer.DrawImage(rcBounds, iimg);
									}
									else
									{
										// image hasn't been saved so far
										bmp = imgDrawer.DrawImage(rcBounds, iimg, true);
										
										if (img->_screenBitmap)
										{
											delete img->_screenBitmap;
											img->_screenBitmap = NULL;
										}

										img->_screenBitmap = bmp;
									}
								}
							}
							else
							{
								imgDrawer.DrawImage(rcBounds, iimg);
							}
						}

						img->Release();
						
						// drawing labels for images
						ILabels* labels = l->get_Labels();
						if(labels != NULL)
						{
							tkVerticalPosition vertPos;
							labels->get_VerticalPosition(&vertPos);
							if (vertPos == vpAboveParentLayer)		
							{
								lblDrawer.DrawLabels(labels);
							}
							labels->Release();
							labels = NULL;
						}
					}
					else if( l->type == ShapefileLayer )
					{
						// grab extents from shapefile in case they've changed
						this->AdjustLayerExtents(m_activeLayers[i]);

						if( l->extents.left   < extents.left   && l->extents.right < extents.left )		continue;
						if( l->extents.left   > extents.right  && l->extents.right > extents.right )	continue;
						if( l->extents.bottom < extents.bottom && l->extents.top   < extents.bottom )	continue;
						if( l->extents.bottom > extents.top    && l->extents.top   > extents.top )		continue;
					
						if( l->object == NULL )
						{
							continue;	// TODO: report the error?
						}
						
						IShapefile* sf = NULL;
						l->object->QueryInterface(IID_IShapefile,(void**)&sf);
						
						/*VARIANT_BOOL vb;
						sf->get_HotTracking(&vb);*/

						if( sf )
						{
							sfDrawer.Draw(rcBounds, sf, ((CShapefile*)sf)->get_File());

							// for old modes we shall mark all the shapes of shapefile as visible as no visiblity expressions were analyzed
							if (m_ShapeDrawingMethod != dmNewSymbology)
							{
								std::vector<ShapeData*>* shapeData = ((CShapefile*)sf)->get_ShapeVector();
								if (shapeData)
								{
									for (size_t n = 0; n < shapeData->size(); n++)
									{
										(*shapeData)[n]->isVisible = true;
									}
								}
							}

							// labels
							ILabels* labels = l->get_Labels();
							if(labels != NULL)
							{
								tkVerticalPosition vertPos;
								labels->get_VerticalPosition(&vertPos);
								if (vertPos == vpAboveParentLayer)		
								{
									lblDrawer.DrawLabels(labels);
								}
								labels->Release();
								labels = NULL;
							}

							// charts: available for all modes
							ICharts* charts = NULL;
							sf->get_Charts(&charts);
							if (charts)
							{
								tkVerticalPosition vertPosition;
								charts->get_VerticalPosition(&vertPosition);
								if (vertPosition == vpAboveParentLayer )
								{
									chartDrawer.DrawCharts(sf);
								}
								charts->Release();
								charts = NULL;
							}
							sf->Release();
						}
					}
				}
			}
		}
	}
	
	// -------------------------------------------------
	//	Drawing labels and charts above the layers
	// -------------------------------------------------
	for (i = 0; i < (int)m_activeLayers.size(); i++)
	{
		Layer * l = m_allLayers[m_activeLayers[i]];
		if( l != NULL )
		{	
			if (l->IsVisible(scale, zoom))
			{
				//  labels: for the new modes only
				if (m_ShapeDrawingMethod == dmNewWithLabels || m_ShapeDrawingMethod == dmNewSymbology || l->type == ImageLayer || FORCE_NEW_LABELS)
				{
					ILabels* labels = l->get_Labels();
					if ( labels )
					{
						tkVerticalPosition vertPos;
						labels->get_VerticalPosition(&vertPos);
						if (vertPos == vpAboveAllLayers)
						{
							lblDrawer.DrawLabels(labels);
						}
						labels->Release(); 
						labels = NULL;
					}
				}
				
				// charts: for all modes
				IShapefile* sf = NULL;
				l->object->QueryInterface(IID_IShapefile,(void**)&sf);
				if ( sf )
				{
					ICharts* charts = NULL;
					sf->get_Charts(&charts);
					if (charts)
					{
						tkVerticalPosition vertPosition;
						charts->get_VerticalPosition(&vertPosition);
						if (vertPosition == vpAboveAllLayers )
						{
							chartDrawer.DrawCharts(sf);
						}
						charts->Release();
						charts = NULL;
					}
					
					sf->Release();
					sf = NULL;
				}
			}
		}
	}

	
	
	m_drawMutex.Unlock();

   if (oldCursor != NULL)
      ::SetCursor(oldCursor);

	delete[] isConcealed;
}
#pragma endregion

#pragma region ImageGrouping
// *****************************************************************
//		BuildImageGroups
// *****************************************************************
// Here we'll make groups from the images with the same size and positions
// Group number will be written to the each image groupID property
void CMapView::BuildImageGroups(std::vector<ImageGroup*>& imageGroups)
{
	imageGroups.clear();

	for(size_t i = 0; i < m_activeLayers.size(); i++)
	{
		Layer * l = m_allLayers[m_activeLayers[i]];
		if( l != NULL )
		{	
			if(l->type == ImageLayer)
			{
				IImage* iimg = NULL;
				l->object->QueryInterface(IID_IImage, (void**)&iimg);

				if ( iimg != NULL )
				{
					CImageClass* img = (CImageClass*)iimg;
					img->m_groupID = -1;
					
					if (l->flags & Visible)
					{
						if ( img->_canUseGrouping)
						{
							double dx, dy, xllCenter, yllCenter;
							LONG width, height;

							img->get_OriginalHeight(&height);
							img->get_OriginalWidth(&width);
							
							img->get_OriginalDX(&dx);
							img->get_OriginalDY(&dy);
							img->get_OriginalXllCenter(&xllCenter);
							img->get_OriginalYllCenter(&yllCenter);

							//img->GetOriginal_dX(&dx);
							//img->GetOriginal_dY(&dy);
							//img->GetOriginalXllCenter(&xllCenter);
							//img->GetOriginalYllCenter(&yllCenter);

							bool groupFound = false;
							for(size_t j = 0; j < imageGroups.size(); j++)
							{
								ImageGroup* group = imageGroups[j];
								
								if ((group->dx == dx) && 
									(group->dy == dy) && 
									(group->width == width) && 
									(group->height == height) &&
									(group->xllCenter == xllCenter) && 
									(group->yllCenter == yllCenter))
								{
									groupFound = true;
									group->imageIndices.push_back(i);
									break;
								}
							}
							
							if (! groupFound )
							{
								// adding new group
								ImageGroup* group = new ImageGroup(dx, dy, xllCenter, yllCenter, width, height);
								imageGroups.push_back(group);
								imageGroups[imageGroups.size() - 1]->imageIndices.push_back(i);
							}
						}
					}
				}
			}
		}
	}

	// now we'll check whether the pixels of image are scarce enough for us
	// the group wil work only in case there is more then 1 suitable image
	int groupId = 0;
	IImage* iimg = NULL;
	for (size_t i = 0; i < imageGroups.size(); i++)
	{
		std::vector<int>* indices = &imageGroups[i]->imageIndices;
		int groupSize = indices->size();

		if (groupSize > 1)
		{
			for (size_t j = 0; j < indices->size(); j++ )
			{
				Layer * l = m_allLayers[m_activeLayers[(*indices)[j]]];
				l->object->QueryInterface(IID_IImage, (void**)&iimg);
				CImageClass* img = (CImageClass*)iimg;
				
				if (!img->_pixelsSaved)				// it's the first time we try to draw image or transparency color chnaged
				{
					if (!img->SaveNotNullPixels())	// analysing pixels...
					{
						(*indices)[j] = -1;
						img->put_CanUseGrouping(VARIANT_FALSE);	//  don't try this image any more - there are to many data pixels in it
						groupSize--;
					}
				}
			}
		}
		
		// saving the valid groups
		if (groupSize > 1)
		{
			imageGroups[i]->isValid = true;
			for (size_t i = 0; i< indices->size(); i++)
			{
				int imageIndex = (*indices)[i];
				if (imageIndex != -1)
				{
					Layer * l = m_allLayers[m_activeLayers[imageIndex]];
					l->object->QueryInterface(IID_IImage, (void**)&iimg);
					CImageClass* img = (CImageClass*)iimg;
					img->m_groupID = groupId;
				}
			}
			groupId++;
		}
		else
		{
			imageGroups[i]->isValid = false;
		}
	}
}

// *****************************************************************
//		ChooseInterpolationMode
// *****************************************************************
// Choosing the mode with better quality from the pair
tkInterpolationMode CMapView::ChooseInterpolationMode(tkInterpolationMode mode1, tkInterpolationMode mode2)
{
	if (mode1 == imHighQualityBicubic || mode2 == imHighQualityBicubic )
	{
		return imHighQualityBicubic;
	}
	else if (mode1 == imHighQualityBilinear || mode2 == imHighQualityBilinear )
	{
		return imHighQualityBilinear;
	}
	else if (mode1 == imBicubic || mode2 == imBicubic )
	{
		return imBicubic;
	}
	else if (mode1 == imBilinear || mode2 == imBilinear )
	{
		return imBilinear;
	}
	else
	{
		return imNone;
	}
}

// *****************************************************************
//		DrawImageGroups
// *****************************************************************
// groupIndex - index of group that should be drawn
void CMapView::DrawImageGroups(const CRect& rcBounds, Gdiplus::Graphics* graphics, int groupIndex)
{
	CImageDrawer imgDrawer(graphics, &extents, m_pixelPerProjectionX, m_pixelPerProjectionY, m_viewWidth, m_viewHeight);
	IImage* iimg = NULL;

	ImageGroup* group = (*m_imageGroups)[groupIndex];
	if (! group->isValid ) 
		return;
	
	// in case the image was drawn at least once at current resolution, we can use screenBitmap
	ScreenBitmap* bmp = NULL;
	bmp = group->screenBitmap;
	if (bmp != NULL)
	{
		if (bmp->extents == extents &&
			bmp->pixelPerProjectionX == m_pixelPerProjectionX &&
			bmp->pixelPerProjectionY == m_pixelPerProjectionY &&
			bmp->viewWidth == m_viewWidth &&
			bmp->viewHeight == m_viewHeight)
		{
			//Gdiplus::Graphics g(dc->m_hDC);
			graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
			
			graphics->SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
			graphics->SetSmoothingMode(Gdiplus::SmoothingModeDefault);
			graphics->SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
			graphics->DrawImage(bmp->bitmap, Gdiplus::REAL(bmp->left), Gdiplus::REAL(bmp->top));
			return;
		}
	}
	
	double scale = GetCurrentScale();
	int zoom;
	this->m_tiles->get_CurrentZoom(&zoom);

	if(group->image == NULL)
	{
		// creating a new temporary image		
		IImage* imgGroup = NULL;
		VARIANT_BOOL vbretval;
		CoCreateInstance(CLSID_Image,NULL,CLSCTX_INPROC_SERVER,IID_IImage,(void**)&imgGroup);
		imgGroup->CreateNew(group->width, group->height, &vbretval);

		if ( !vbretval )
		{
			return;
		}
		else
		{
			// setting it's position
			imgGroup->put_dX(group->dx);
			imgGroup->put_dY(group->dy);
			imgGroup->put_XllCenter(group->xllCenter);
			imgGroup->put_YllCenter(group->yllCenter);
			
			tkInterpolationMode downsamplingMode = imNone;
			tkInterpolationMode upsamplingMode = imNone;
			
			// acquiring reference to the destination color array
			unsigned char* data = ((CImageClass*)imgGroup)->get_ImageData();
			colour* dstData = reinterpret_cast<colour*>(data);
			
			// passing the data from all images
			bool visibleLayerExists = false;
			bool useTransparencyColor = true;		
			for(size_t j = 0; j < m_activeLayers.size(); j++)
			{
				Layer * l = m_allLayers[m_activeLayers[j]];
				if( l != NULL )
				{	
					//if(l->type == ImageLayer && (l->flags & Visible))
					if(l->type == ImageLayer && l->IsVisible(scale, zoom))
					{
						l->object->QueryInterface(IID_IImage, (void**)&iimg);
						CImageClass* img = (CImageClass*)iimg;

						if ( img )
						{
							if (img->m_groupID == groupIndex)
							{
								tkInterpolationMode downMode;
								tkInterpolationMode upMode;
								img->get_DownsamplingMode(&downMode);
								img->get_UpsamplingMode(&upMode);
								
								// in case at least one image don't use transparency the grouped bitmap will have white background
								VARIANT_BOOL transp;
								img->get_UseTransparencyColor(&transp);
								if (!transp) 
									useTransparencyColor = false;

								downsamplingMode = ChooseInterpolationMode(downsamplingMode, downMode);
								upsamplingMode = ChooseInterpolationMode(upsamplingMode, upMode);
								
								visibleLayerExists = true;

								DataPixels* pixels = img->m_pixels;
								int pixelsCount = img->m_pixelsCount;

								// passing data
								DataPixels* val;
								for (int p = 0; p < pixelsCount; p++ )
								{
									val = pixels + p;
									memcpy(&(dstData[val->position]), &val->value, sizeof(colour));
									//dstData[val->position] = val->value;
								}
							}
						}
					}
				}
			}
			
			if (useTransparencyColor)
			{
				imgGroup->put_TransparencyColor(RGB(255, 255, 255));
				imgGroup->put_TransparencyColor2(RGB(255, 255, 255));
				imgGroup->put_UseTransparencyColor(VARIANT_TRUE);
			}
			else
			{
				imgGroup->put_UseTransparencyColor(VARIANT_FALSE);
			}

			if (!visibleLayerExists)
			{
				return;
			}
			else
			{
				// setting sampling mode
				imgGroup->put_UpsamplingMode(upsamplingMode);
				imgGroup->put_DownsamplingMode(downsamplingMode);
				group->image = imgGroup;
			}
		}
	}
	
	// drawing; in case we draw it first time screen bitmap will be saved, for not doing resampling when loading each new tile
	/* ScreenBitmap*  */
	bmp = imgDrawer.DrawImage(rcBounds, group->image, true);
	if (bmp)
	{
		if (group->screenBitmap != NULL)
		{
			delete group->screenBitmap;
			group->screenBitmap = NULL;
		}
		
		int width = bmp->bitmap->GetWidth();
		int height = bmp->bitmap->GetHeight();
		
		group->screenBitmap = bmp;	// saving bitmap in screen resolution
	}
}

// *****************************************************************
//		ImageGroupsAreEqual()
// *****************************************************************
bool CMapView::ImageGroupsAreEqual(std::vector<ImageGroup*>& groups1, std::vector<ImageGroup*>& groups2)
{
	if (groups1.size() != groups2.size())
	{
		return false;
	}
	else
	{
		for (size_t i = 0; i < groups1.size(); i++)
		{
			if (!(groups1[i] == groups2[i]))
			{
				return false;
			}
		}
	}
	return true;
}
#pragma endregion

#pragma region REGION SnapShots

// *********************************************************
//		SnapShot()
// *********************************************************
LPDISPATCH CMapView::SnapShot(LPDISPATCH BoundBox)
{
	if( BoundBox == NULL )
	{	
		ErrorMessage(tkUNEXPECTED_NULL_PARAMETER);
		return NULL;
	}

	IExtents * box = NULL;
	BoundBox->QueryInterface(IID_IExtents,(void**)&box);

	if( box == NULL )
	{	
		ErrorMessage(tkINTERFACE_NOT_SUPPORTED);
		return NULL;
	}

	double left, right, bottom, top, nv;
	box->GetBounds(&left,&bottom,&nv,&right,&top,&nv);
	box->Release();
	box = NULL;
	
	return SnapShotCore(left, right, bottom, top, m_viewWidth, m_viewHeight);
}

// *********************************************************
//		SnapShot2()
// *********************************************************
// use the indicated layer and zoom/width to determine the output size and clipping
IDispatch* CMapView::SnapShot2(LONG ClippingLayerNbr, DOUBLE Zoom, long pWidth)
{   
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	long Width, Height;
	double left, right, bottom, top;

	Layer * l = m_allLayers[ClippingLayerNbr];
	if( !IS_VALID_PTR(l) )
	{
		if( m_globalCallback != NULL )
			m_globalCallback->Error(m_key.AllocSysString(),A2BSTR("Cannot clip to selected layer"));
		return NULL;
	}
	else
	{	
		this->AdjustLayerExtents(ClippingLayerNbr);
		left = l->extents.left;
		right = l->extents.right;
		top = l->extents.top;
		bottom = l->extents.bottom;

		if( l->type == ShapefileLayer )
		{
			double ar = (right-left)/(top-bottom);
			Width = (long) pWidth == 0 ? ((right - left) * Zoom) : pWidth;
			Height = (long)((double)pWidth / ar);
		}
		else if(l->type == ImageLayer)
		{
			Width = right - left;
			Height = top - bottom;
			if (Zoom > 0)
			{
				Width *= (long)Zoom;
				Height *= (long)Zoom;
			}
		}
		else
		{
			if( m_globalCallback != NULL )
				m_globalCallback->Error(m_key.AllocSysString(),A2BSTR("Cannot clip to selected layer type"));
			return NULL;
		}
	}

	if (Width <= 0 || Height <= 0)
	{
		if( m_globalCallback != NULL )
			m_globalCallback->Error(m_key.AllocSysString(),A2BSTR("Invalid Width and/or Zoom"));
		return NULL;
	}

	return this->SnapShotCore(left, right, top, bottom, Width, Height);
}

//Created a new snapshot method which works a bit better specifically for the printing engine
//1. Draw to a back buffer, 2. Populate an Image object
LPDISPATCH CMapView::SnapShot3(double left, double right, double top, double bottom, long Width)
{   
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	long Height = (long)((double)Width / ((right-left)/(top-bottom)));
	if (Width <= 0 || Height <= 0)
	{
		if( m_globalCallback != NULL )
			m_globalCallback->Error(m_key.AllocSysString(),A2BSTR("Invalid Width and/or Zoom"));
		return NULL;
	}

	return this->SnapShotCore(left, right, top, bottom, Width, Height);
}

// *********************************************************************
//    LoadTiles()
// *********************************************************************
// Loads tiles for specified extents
void CMapView::LoadTiles(IExtents* Extents, LONG WidthPixels, LPCTSTR Key, tkTileProvider provider)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	USES_CONVERSION;
	
	// Get the image height based on the box aspect ratio
	double xMin, xMax, yMin, yMax, zMin, zMax;
	Extents->GetBounds(&xMin, &yMin, &zMin, &xMax, &yMax, &zMax);
	
	// Make sure that the width and height are valid
	long Height = static_cast<long>((double)WidthPixels *(yMax - yMin) / (xMax - xMin));
	if (WidthPixels <= 0 || Height <= 0)
	{
		if( m_globalCallback != NULL )
			m_globalCallback->Error(m_key.AllocSysString(), A2BSTR("Invalid Width and/or Zoom"));
	}
	else
	{
		CString key = (char*)Key;
		SetTempExtents(xMin, xMax, yMin, yMax, WidthPixels, Height);
		bool tilesInCache =((CTiles*)m_tiles)->TilesAreInCache((void*)this, provider);
		if (!tilesInCache)
		{
			((CTiles*)m_tiles)->LoadTiles((void*)this, true, (int)provider, key);
			RestoreExtents();
		}
		else
		{
			// they are already here, no loading is needed
			RestoreExtents();
			FireTilesLoaded(m_tiles, NULL, true, key);
		}
	}
}

BOOL CMapView::SnapShotToDC2(PVOID hdc, IExtents* Extents, LONG Width, float OffsetX, float OffsetY,
							 float ClipX, float ClipY, float clipWidth, float clipHeight)
{
	if(!Extents || !hdc) 
	{
		ErrorMessage(tkUNEXPECTED_NULL_PARAMETER);
		return FALSE;
	}
	// getting DC to draw
	HDC dc = reinterpret_cast<HDC>(hdc);
	CDC * tempDC = CDC::FromHandle(dc);

	// Get the image height based on the box aspect ration
	double xMin, xMax, yMin, yMax, zMin, zMax;
	Extents->GetBounds(&xMin, &yMin, &zMin, &xMax, &yMax, &zMax);
	
	// Make sure that the width and height are valid
	long Height = static_cast<long>((double)Width *(yMax - yMin) / (xMax - xMin));
	if (Width <= 0 || Height <= 0)
	{
		if( m_globalCallback != NULL )
			m_globalCallback->Error(m_key.AllocSysString(), A2BSTR("Invalid Width and/or Zoom"));
		return FALSE;
	}
	
	//Debug::WriteLine("Dpi: %f", g->GetDpiX());
	//Gdiplus::Matrix m;
	//g->GetTransform(&m);
	//Debug::WriteLine("Offset X: %f", m.OffsetX());
	//Debug::WriteLine("Offset Y: %f", m.OffsetY());

	/*Gdiplus::Graphics* g = Gdiplus::Graphics::FromHDC(dc);
	g->TranslateTransform(OffsetX, OffsetY);
	Gdiplus::RectF r(0,0,100, 100);
	Gdiplus::SolidBrush br(Gdiplus::Color::Gray);
	g->FillRectangle(&br, r);
	g->ReleaseHDC(dc);
	delete g;*/

	//CRect r(50, 50, 150, 150);
	//COLORREF clr = 255;
	//CBrush brush(clr);
	//tempDC->FillRect(r, &brush);

	SnapShotCore(xMin, xMax, yMin, yMax, Width, Height, tempDC, OffsetX, OffsetY, ClipX, ClipY, clipWidth, clipHeight);
	return TRUE;
}

// *********************************************************************
//    SnapShotToDC()
// *********************************************************************
// Draws the specified extents of map at given DC.
BOOL CMapView::SnapShotToDC(PVOID hdc, IExtents* Extents, LONG Width)
{
	return this->SnapShotToDC2(hdc, Extents, Width, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
}

#pragma region Swtich Extents
void CMapView::SetTempExtents(double left, double right, double top, double bottom, long Width, long Height)
{
	mm_viewWidth = m_viewWidth;
	mm_viewHeight = m_viewHeight;
	mm_pixelPerProjectionX = m_pixelPerProjectionX;
	mm_pixelPerProjectionY = m_pixelPerProjectionY;
	mm_inversePixelPerProjectionX = m_inversePixelPerProjectionX;
	mm_inversePixelPerProjectionY = m_inversePixelPerProjectionY;
	mm_aspectRatio = m_aspectRatio;
	mm_left = extents.left;
	mm_right = extents.right;
	mm_bottom = extents.bottom;
	mm_top = extents.top;

	mm_newExtents = (Width != m_viewWidth || Height != m_viewWidth ||
						left != extents.left || right !=  extents.right ||
						top != extents.top || bottom != extents.bottom);

	if (mm_newExtents)
	{
		m_viewWidth=Width;
		m_viewHeight=Height;
		//ResizeBuffers(m_viewWidth, m_viewHeight);
		m_aspectRatio = (double)Width / (double)Height; 

		double xrange = right - left;
		double yrange = top - bottom;
		m_pixelPerProjectionX = m_viewWidth/xrange;
		m_inversePixelPerProjectionX = 1.0/m_pixelPerProjectionX;
		m_pixelPerProjectionY = m_viewHeight/yrange;
		m_inversePixelPerProjectionY = 1.0/m_pixelPerProjectionY;
		
		extents.left = left;
		extents.right = right - m_inversePixelPerProjectionX;
		extents.bottom = bottom;
		extents.top = top - m_inversePixelPerProjectionY;

		CalculateVisibleExtents(Extent(left,right,bottom,top));
	}
}

void CMapView::RestoreExtents()
{
	if (mm_newExtents)
	{
		m_viewWidth = mm_viewWidth;
		m_viewHeight = mm_viewHeight;
		//ResizeBuffers(m_viewWidth, m_viewHeight);
		m_aspectRatio = mm_aspectRatio; 
		m_pixelPerProjectionX = mm_pixelPerProjectionX;
		m_pixelPerProjectionY = mm_pixelPerProjectionY;
		m_inversePixelPerProjectionX = mm_inversePixelPerProjectionX;
		m_inversePixelPerProjectionY = mm_inversePixelPerProjectionY;
		extents.left = mm_left;
		extents.right = mm_right;
		extents.bottom = mm_bottom;
		extents.top = mm_top;
	}
}
#pragma endregion


// *********************************************************
//		SnapShotCore()
// *********************************************************
// first 4 paramters - extents in map units; last 2 - the size of bitmap to draw this extents on
IDispatch* CMapView::SnapShotCore(double left, double right, double top, double bottom, long Width, long Height, CDC* snapDC,
								  float offsetX, float offsetY, float clipX, float clipY, float clipWidth, float clipHeight)
{
	bool createDC = (snapDC == NULL);
	CBitmap * bmp = NULL;
	
	if (createDC)
	{
		bmp = new CBitmap();
		if (!bmp->CreateDiscardableBitmap(GetDC(), Width, Height))
		{
			delete bmp;
			if( m_globalCallback != NULL )
				m_globalCallback->Error(m_key.AllocSysString(),A2BSTR("Failed to create bitmap; not enough memory?"));
			return NULL;
		}
	}

	LockWindow( lmLock );

	SetTempExtents(left, right, top, bottom, Width, Height);

	// saving the state of (do this even in case no new extents are needed, for not to hide declarations in the block)
	//long mm_viewWidth = m_viewWidth;
	//long mm_viewHeight = m_viewHeight;
	//double mm_pixelPerProjectionX = m_pixelPerProjectionX;
	//double mm_pixelPerProjectionY = m_pixelPerProjectionY;
	//double mm_inversePixelPerProjectionX = m_inversePixelPerProjectionX;
	//double mm_inversePixelPerProjectionY = m_inversePixelPerProjectionY;
	//double mm_aspectRatio = m_aspectRatio;
	//double mm_left = extents.left;
	//double mm_right = extents.right;
	//double mm_bottom = extents.bottom;
	//double mm_top = extents.top;

	//// calculating new bounds
	//if (newExtents)
	//{
	//	m_viewWidth=Width;
	//	m_viewHeight=Height;
	//	//ResizeBuffers(m_viewWidth, m_viewHeight);
	//	m_aspectRatio = (double)Width / (double)Height; 

	//	double xrange = right - left;
	//	double yrange = top - bottom;
	//	m_pixelPerProjectionX = m_viewWidth/xrange;
	//	m_inversePixelPerProjectionX = 1.0/m_pixelPerProjectionX;
	//	m_pixelPerProjectionY = m_viewHeight/yrange;
	//	m_inversePixelPerProjectionY = 1.0/m_pixelPerProjectionY;
	//	
	//	extents.left = left;
	//	extents.right = right - m_inversePixelPerProjectionX;
	//	extents.bottom = bottom;
	//	extents.top = top - m_inversePixelPerProjectionY;

	//	CalculateVisibleExtents(Extent(left,right,bottom,top));
	//}

	if (mm_newExtents)
	{
		ReloadImageBuffers();
		((CTiles*)m_tiles)->MarkUndrawn();		// otherwise they will be taken from screen buffer
	}

	IImage * iimg = NULL;
	bool tilesInCache = false;

	// create canvas
	CBitmap * oldBMP = NULL;
	if (createDC)
	{
		snapDC = new CDC();
		snapDC->CreateCompatibleDC(GetDC());
		oldBMP = snapDC->SelectObject(bmp);
	}
	
	// do the drawing
	m_canbitblt=FALSE;
	m_isSnapshot = true;

	tilesInCache =((CTiles*)m_tiles)->TilesAreInCache((void*)this);
	if (tilesInCache)
	{
		((CTiles*)m_tiles)->LoadTiles((void*)this, true);		// simply move the to the screen buffer (is performed synchronously)
	}

	CRect rcBounds(0,0,m_viewWidth,m_viewHeight);
	if (clipWidth != 0.0 && clipHeight != 0.0)
	{
		CRect rcClip(clipX, clipY, clipWidth, clipHeight);
		HandleNewDrawing(snapDC, rcBounds, rcClip, offsetX, offsetY);
	}
	else
	{
		HandleNewDrawing(snapDC, rcBounds, rcBounds, offsetX, offsetY);
	}

	m_canbitblt=FALSE;
	m_isSnapshot = false;

	if (createDC)
	{
		// create output
		VARIANT_BOOL retval;
		CoCreateInstance(CLSID_Image,NULL,CLSCTX_INPROC_SERVER,IID_IImage,(void**)&iimg);
		iimg->SetImageBitsDC((long)snapDC->m_hDC,&retval);

		double dx = (right-left)/(double)(m_viewWidth);
		double dy = (top-bottom)/(double)(m_viewHeight);
		iimg->put_dX(dx);
		iimg->put_dY(dy);
		iimg->put_XllCenter(left + dx*.5);
		iimg->put_YllCenter(bottom + dy*.5);
	
		// dispose the canvas
		snapDC->SelectObject(oldBMP);
		bmp->DeleteObject();
		snapDC->DeleteDC();
		delete bmp;
		delete snapDC;
	}

	RestoreExtents();

	if (mm_newExtents)
	{
		this->ReloadImageBuffers();
		mm_newExtents = false;
	}

	// restore the previous state
	//if (newExtents)
	//{
	//	m_viewWidth = mm_viewWidth;
	//	m_viewHeight = mm_viewHeight;
	//	//ResizeBuffers(m_viewWidth, m_viewHeight);
	//	m_aspectRatio = mm_aspectRatio; 
	//	m_pixelPerProjectionX = mm_pixelPerProjectionX;
	//	m_pixelPerProjectionY = mm_pixelPerProjectionY;
	//	m_inversePixelPerProjectionX = mm_inversePixelPerProjectionX;
	//	m_inversePixelPerProjectionY = mm_inversePixelPerProjectionY;
	//	extents.left = mm_left;
	//	extents.right = mm_right;
	//	extents.bottom = mm_bottom;
	//	extents.top = mm_top;
	//	this->ReloadImageBuffers();
	//}
	
	if (tilesInCache)
	{
		((CTiles*)m_tiles)->LoadTiles((void*)this, false);	  // restore former list of tiles in the buffer
	}

	LockWindow( lmUnlock );
	return iimg;
}

// ********************************************************************
//		DrawBackBuffer()
// ********************************************************************
// Draws the backbuffer to the specified DC (probably external)
void CMapView::DrawBackBuffer(int** hdc, int ImageWidth, int ImageHeight)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	if (!hdc)
	{
		m_lastErrorCode = tkUNEXPECTED_NULL_PARAMETER;
		return;
	}
	
	CDC* dc = CDC::FromHandle((HDC)hdc);
	CRect rect(0,0, ImageWidth, ImageHeight);
	OnDraw(dc, rect, rect);
}
#pragma endregion

#pragma region Scalebar + maptext
// ****************************************************************
//		GetUnitOfMeasureText
// ****************************************************************
// Returns the short nam� for units of measure
CString GetUnitOfMeasureText(tkUnitsOfMeasure units)
{
	switch(units)
	{
		case umDecimalDegrees:
			return "deg.";
		case umMiliMeters:
			return "mm";
		case umCentimeters:
			return "cm";
		case umInches:
			return "inches";
		case umFeets:
			return "feet";
		case umYards:
			return "yards";
		case umMeters:
			return "m";
		case umMiles:
			return "miles";
		case umKilometers:
			return "km";
		default:
			return "units";
	}
}

// ****************************************************************
//		DrawScaleBar()
// ****************************************************************
void CMapView::DrawScaleBar(Gdiplus::Graphics* g)
{
	if (m_transformationMode != tkTransformationMode::tmNotDefined)
	{
		int zoom = -1;
		m_tiles->get_CurrentZoom(&zoom);
		if (zoom >= 0 && zoom < 3) {
			// lsu: there are some problems with displaying scalebar at such zoom levels: 
			// - there are areas outside the globe where coordinate transformations may fail;
			// - the points at the left and right sides of the screen may lie on the same meridian
			// so geodesic distance across the screen will be 0;
			// - finally projection distortions change drastically by Y axis across map so
			// the scalebar will be virtually meaningless;
			// The easy solution will be simply not to show scalebar at such small scales
			return;
		}
	}
	
	double minX, maxX, minY, maxY;	// size of ap control in pixels
    PROJECTION_TO_PIXEL(extents.left, extents.bottom, minX, minY);
	PROJECTION_TO_PIXEL(extents.right, extents.top, maxX, maxY);
	
	int barWidth = 140;
	int barHeight = 30;
	int yPadding = 10;
	int xPadding = 10;
	int xOffset = 20;
	int yOffset = 10;
	int segmHeight = 7;

	Gdiplus::SmoothingMode smoothing = g->GetSmoothingMode();
	Gdiplus::TextRenderingHint hint = g->GetTextRenderingHint();
	g->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	g->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

	tkUnitsOfMeasure targetUnits = umMeters;

	double xMin = extents.left;
	double yMin = extents.top;
	double xMax = extents.right;
	double yMax = extents.bottom;
	
	double width = extents.right - extents.left; // maxX - minX;

	// run calculations on ellipsoid
	if (m_transformationMode != tkTransformationMode::tmNotDefined)
	{
		// skip calculations when extents haven't changed
		if (this->m_lastWidthMeters == 0.0)
		{
			bool skipTransform = false;
			if (m_transformationMode == tkTransformationMode::tmDoTransformation)
			{
				VARIANT_BOOL vb;
				m_projection->get_HasTransformation(&vb);
				if (!vb) {
					m_projection->StartTransform(m_wgsProjection, &vb);
				}
				if (vb)
				{
					m_projection->Transform(&xMin, &yMin, &vb);
					m_projection->Transform(&xMax, &yMax, &vb);
				}
				else {
					skipTransform = true;
				}
			}
		
			if ( !skipTransform)
			{
				GetUtils()->GeodesicDistance((yMax + yMin)/2, xMin, (yMax + yMin)/2, xMax, &width);
				m_lastWidthMeters = width;
			}
		}
		else
		{
			width = m_lastWidthMeters;
		}
	}

	if (width != 0.0)
	{
		if( Utility::ConvertDistance(m_unitsOfMeasure, targetUnits, width))
		{
			double unitsPerPixel = width/(maxX - minX);	  // target units on screen size
			double distance = (barWidth - xPadding * 2) * unitsPerPixel;

			if (distance > 1000)
			{
				targetUnits = umKilometers;
				unitsPerPixel /= 1000.0;
				distance /= 1000.0;
			}

			if (distance < 1)
			{
				targetUnits = umCentimeters;
				unitsPerPixel *= 100.0;
				distance *= 100.0;
			}
		
			double power = floor(log10(distance));
			double step = pow(10, floor(log10(distance)));
			int count = (int)floor(distance/step);

			if (count == 1)
			{
				step /= 4;	// steps like 25-50-75
				count = (int)floor(distance/step);
			}

			if (count == 2)
			{
				step /= 2;	// steps like 0-50-100
				count = (int)floor(distance/step);
			}

			if (count > 8)
			{
				step *= 2.5;
				count = (int)floor(distance/step);
			}
			
			//Gdiplus::SolidBrush brush(Gdiplus::Color::White);
			Gdiplus::Pen pen(Gdiplus::Color::Black, 1.5f);
			Gdiplus::Pen penOutline(Gdiplus::Color::White, 3.0f);
			pen.SetLineJoin(Gdiplus::LineJoinRound);
			penOutline.SetLineJoin(Gdiplus::LineJoinRound);
			Gdiplus::Matrix mtx;
			Gdiplus::RectF rect(0.0f, 0.0f, (Gdiplus::REAL)barWidth, (Gdiplus::REAL)barHeight );

			// initializing font
			Gdiplus::FontFamily family(L"Arial");
			Gdiplus::Font font(&family, (Gdiplus::REAL)12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
			
			CStringW s;
			Gdiplus::StringFormat format; 
			Gdiplus::GraphicsPath path;

			if (m_viewWidth > barWidth + xOffset &&
				m_viewHeight > barHeight + yOffset)		// control must be big enough
			{
				mtx.Translate((float)5, (float)m_viewHeight - barHeight - yOffset);
				g->SetTransform(&mtx);
				
				int length = (int)(step * count / unitsPerPixel + xPadding);

				// horizontal line
				g->DrawLine(&penOutline, xPadding,  barHeight - yPadding, length, barHeight - yPadding);
				g->DrawLine(&pen, xPadding,  barHeight - yPadding, length, barHeight - yPadding);
				
				// inner measures (shorter)
				for (int i = 0; i <= count; i++ )
				{
					length = (int)(step * i / unitsPerPixel + xPadding);
					int valHeight = (i == 0 || i == count) ? segmHeight * 2 : segmHeight;	// the height of the mark; side marks are longer
					g->DrawLine(&penOutline, length,  barHeight - yPadding - valHeight, length, barHeight - yPadding);
					g->DrawLine(&pen, length,  barHeight - yPadding - valHeight, length, barHeight - yPadding);
				}

				Gdiplus::Pen penText(Gdiplus::Color::White, 3.0f);
				Gdiplus::SolidBrush brushText(Gdiplus::Color::Black);

				s.Format(L"0");
				Gdiplus::PointF point(xPadding + 3.0f, -2.0f);
				path.StartFigure();
				path.AddString(s.GetString(), wcslen(s), &family, font.GetStyle(), font.GetSize(), point, &format);
				path.CloseFigure();
				g->DrawPath(&penText, &path);
				g->FillPath(&brushText, &path);

				if (power >= 0)
				{
					s.Format(L"%d",(int)(step * count));
				}
				else
				{
					CStringW sFormat;
					sFormat.Format(L"%%.%df", (int)-power);
					s.Format(sFormat,(float)step * count);
				}
				
				point.X = (Gdiplus::REAL)(step * count/unitsPerPixel + xPadding + 3);
				point.Y = -2.0f;	//yPadding
				path.StartFigure();
				path.AddString(s.GetString(), wcslen(s), &family, font.GetStyle(), font.GetSize(), point, &format);
				path.CloseFigure();
				g->DrawPath(&penText, &path);
				g->FillPath(&brushText, &path);

				s = GetUnitOfMeasureText(targetUnits);
				
				point.X = (Gdiplus::REAL)(step * count/unitsPerPixel + xPadding + 3);
				point.Y = (Gdiplus::REAL)(barHeight - yPadding - 12);
				path.StartFigure();
				path.AddString(s.GetString(), wcslen(s), &family, font.GetStyle(), font.GetSize(), point, &format);
				path.CloseFigure();
				g->DrawPath(&penText, &path);
				g->FillPath(&brushText, &path);

				g->ResetTransform();
			}

			/*CString s;
			s.Format("Invalid number of breaks: %d", count);	

			if (count < 2 || count > 10)
				AfxMessageBox(s);*/
		}
	}

	g->SetTextRenderingHint(hint);
	g->SetSmoothingMode(smoothing);
}

// ****************************************************************
//		DrawMeasuring()
// ****************************************************************
void CMapView::DrawMeasuring(Gdiplus::Graphics* g )
{
	// transparency
	// color
	// width
	// style
	// vertex size

	CMeasuring* measuring = ((CMeasuring*)m_measuring);
	if (measuring)
	{
		int size = measuring->points.size();
		if (size > 0)
		{
			Gdiplus::PointF* data = new Gdiplus::PointF[size];
			for(size_t i = 0; i < measuring->points.size(); i++) {
				double x, y;
				this->ProjectionToPixel(measuring->points[i].x, measuring->points[i].y, x, y);
				data[i].X = (Gdiplus::REAL)x;
				data[i].Y = (Gdiplus::REAL)y;
			}
			Gdiplus::Pen pen(Gdiplus::Color::Orange, 2.0f);
			g->DrawLines(&pen, data, size);
			delete[] data;
		}
	}
}

// ****************************************************************
//		ShowRedrawTime()
// ****************************************************************
// Displays redraw time in the bottom left corner
void CMapView::ShowRedrawTime(Gdiplus::Graphics* g, float time, CStringW message )
{
	if (!m_ShowRedrawTime && !m_ShowVersionNumber)	return;

	// preparing canvas
	Gdiplus::SmoothingMode smoothing = g->GetSmoothingMode();
	Gdiplus::TextRenderingHint hint = g->GetTextRenderingHint();
	g->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	g->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

	// initializing brushes
	Gdiplus::SolidBrush brush(Gdiplus::Color::Black);
	Gdiplus::Pen pen(Gdiplus::Color::White, 3.0f);
	pen.SetLineJoin(Gdiplus::LineJoinRound);

	// initializing font
	Gdiplus::FontFamily family(L"Arial");
	Gdiplus::Font font(&family, (Gdiplus::REAL)12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
	Gdiplus::PointF point(0.0f, 0.0f);
	Gdiplus::StringFormat format; 
	
	// vars
	CStringW s;
	Gdiplus::GraphicsPath path;
	Gdiplus::RectF rect;
	Gdiplus::Matrix mtx;
	
	Gdiplus::REAL width = (Gdiplus::REAL)m_viewWidth; //r.right - r.left;
	Gdiplus::REAL height;

	if (m_ShowVersionNumber)
	{
		s.Format(L"MapWinGIS %d.%d", _wVerMajor, _wVerMinor);
		path.StartFigure();
		path.AddString(s.GetString(), wcslen(s), &family, font.GetStyle(), font.GetSize(), point, &format);
		path.CloseFigure();
		path.GetBounds(&rect);
		height = rect.Height;
		
		if (rect.Width + 10 < width)		// control must be big enough to host the string
		{
			mtx.Translate((float)(m_viewWidth - rect.Width - 10), (float)(m_viewHeight - height - 10));
			path.Transform(&mtx);
			g->DrawPath(&pen, &path);
			g->FillPath(&brush, &path);
			width -= (rect.Width);
		}
	}
	
	path.Reset();
	mtx.Reset();

	if (m_ShowRedrawTime)
	{
		if (wcslen(message) != 0)
		{
			s = message;
		}
		else
		{
			s.Format(L"Redraw time: %.3f sec", time);
		}
		path.StartFigure();
		path.AddString(s.GetString(), wcslen(s), &family, font.GetStyle(), font.GetSize(), point, &format);
		path.CloseFigure();
		path.GetBounds(&rect);
		height = m_ShowVersionNumber?height:rect.Height + 3;
		if (rect.Width + 15 < width)		// control must be big enough to host the string
		{
			mtx.Translate(5.0f, (float)(m_viewHeight - height - 10));
			path.Transform(&mtx);
			g->DrawPath(&pen, &path);
			g->FillPath(&brush, &path);
			width -= (rect.Width + 15);
		}
	}
	
	g->SetTextRenderingHint(hint);
	g->SetSmoothingMode(smoothing);
}
#pragma endregion

#pragma region Multithreading
// ******************************************************************
//		InitMapRotation()
// ******************************************************************
//	Chnages the nessary variables to perform drawing with rotation
void InitMapRotation()
{
	// TODO: implement
}

// ******************************************************************
//		CloseMapRotation()
// ******************************************************************
// Restores the variables back after the drawing
void CloseMapRotation()
{
	// TODO: implement
}

// A structure to pass parameters to the background thread
struct DrawingParams: CObject 
{
	Gdiplus::Graphics* graphics;
	const CRect* bounds;
	CMapView* map;

	DrawingParams(CMapView* m, Gdiplus::Graphics* g, const CRect* b)
	{
		graphics = g;
		bounds = b;
		map = m;
	};
};

// ***************************************************************
//		StartDrawLayers()
// ***************************************************************
// Starts drawing in the background thread
UINT CMapView::StartDrawLayers(LPVOID pParam)
{
	DrawingParams* options = (DrawingParams*)pParam;
	if (!options || !options->IsKindOf(RUNTIME_CLASS(DrawingParams)))
	{
		return 0;   // if pObject is not valid
	}
	else
	{
		this->DrawLayers(options->bounds, options->graphics);
		return 1;   // thread completed successfully
	}
}


UINT StartThread(LPVOID pParam)
{
	DrawingParams* options = (DrawingParams*)pParam;
	if (!options || !options->IsKindOf(RUNTIME_CLASS(DrawingParams)))
	{
		return 0;   // if pObject is not valid
	}
	else
	{
		CMapView* map = options->map;
		map->DrawLayers(options->bounds, options->graphics);
		return 1;   // thread completed successfully
	}
}
#pragma endregion